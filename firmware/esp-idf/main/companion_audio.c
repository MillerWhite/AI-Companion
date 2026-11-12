#include "companion_audio.h"

#include <math.h>
#include <string.h>
#include "esp_codec_dev.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "bsp/esp-bsp.h"

#define AUDIO_RATE 16000
#define AUDIO_FRAME_SAMPLES 320
#define AUDIO_COOLDOWN_US 5200000LL
#define VAD_RMS_THRESHOLD 460.0f
#define VAD_END_SILENCE_FRAMES 32
#define VAD_START_FRAMES 3
#define AUDIO_MAX_UTTERANCE_MS 6500

static const char *TAG = "companion_audio";
static esp_codec_dev_handle_t speaker;
static esp_codec_dev_handle_t microphone;
static bool audio_ready;
static int64_t last_sound_us;
static volatile bool playback_active;
static uint8_t sound_temperament;

bool companion_audio_is_ready(void) { return audio_ready; }

void companion_audio_set_temperament(uint8_t temperament)
{
    sound_temperament = temperament <= 4 ? temperament : 0;
}

esp_err_t companion_audio_init(void)
{
    if (audio_ready) return ESP_OK;
    speaker = bsp_audio_codec_speaker_init();
    microphone = bsp_audio_codec_microphone_init();
    if (!speaker || !microphone) return ESP_FAIL;

    const esp_codec_dev_sample_info_t format = {
        .sample_rate = AUDIO_RATE,
        .channel = 2,
        .bits_per_sample = 16,
    };
    /* Waveshare's 1.75C reference example closes and reopens both codec
       handles when changing I2S sample format. Keep the board BSP as owner
       of codec/I2S pins and amplifier control. */
    (void)esp_codec_dev_close(speaker);
    (void)esp_codec_dev_close(microphone);
    ESP_RETURN_ON_ERROR(esp_codec_dev_set_in_gain(microphone, 18.0f), TAG, "microphone gain setup failed");
    ESP_RETURN_ON_ERROR(esp_codec_dev_open(speaker, &format), TAG, "speaker codec open failed");
    ESP_RETURN_ON_ERROR(esp_codec_dev_set_out_vol(speaker, 18), TAG, "speaker volume setup failed");
    ESP_RETURN_ON_ERROR(esp_codec_dev_open(microphone, &format), TAG, "microphone codec open failed");
    audio_ready = true;
    ESP_LOGI(TAG, "ES8311 speaker and ES7210 microphones initialized at 16 kHz");
    return ESP_OK;
}

static float emotion_frequency(companion_emotion_t emotion)
{
    static const float base[COMPANION_EMOTION_COUNT] = {
        392.0f, 660.0f, 740.0f, 240.0f, 300.0f,
        520.0f, 820.0f, 900.0f, 330.0f, 470.0f
    };
    return base[emotion < COMPANION_EMOTION_COUNT ? emotion : COMPANION_CALM];
}

esp_err_t companion_audio_play(companion_emotion_t emotion, bool force)
{
    if (!audio_ready) return ESP_ERR_INVALID_STATE;
    if (playback_active) return ESP_ERR_TIMEOUT;
    const int64_t now = esp_timer_get_time();
    if (!force && now - last_sound_us < AUDIO_COOLDOWN_US) return ESP_ERR_TIMEOUT;
    last_sound_us = now;

    /* Short, soft, nonverbal motif. No speech, no continuous loop, conservative gain. */
    static int16_t pcm[2 * 9600];
    const float temperament_ratio[] = {1.0f, 0.91f, 1.16f, 0.88f, 0.96f};
    const float base = emotion_frequency(emotion) * temperament_ratio[sound_temperament];
    const float note_ratio[3] = {1.0f, 1.19f, 1.42f};
    const size_t note_len[3] = {2400, 2100, 2700};
    size_t offset = 0;
    for (int note = 0; note < 3; ++note) {
        const size_t count = note_len[note];
        for (size_t i = 0; i < count; ++i) {
            const float t = (float)i / AUDIO_RATE;
            const float progress = (float)i / (float)count;
            const float envelope = sinf(3.1415926f * progress) * (1.0f - 0.32f * progress);
            const float f = base * note_ratio[note];
            const float fundamental = sinf(6.2831853f * f * t);
            const float overtone = sinf(6.2831853f * f * 2.01f * t) * 0.16f;
            const int16_t sample = (int16_t)((fundamental + overtone) * envelope * 1050.0f);
            pcm[(offset + i) * 2] = sample;
            pcm[(offset + i) * 2 + 1] = sample;
        }
        offset += count;
        offset += 420; /* deliberate silence between notes */
    }
    playback_active = true;
    esp_err_t err = esp_codec_dev_write(speaker, pcm, offset * 2 * sizeof(int16_t));
    playback_active = false;
    return err;
}

esp_err_t companion_audio_capture_utterance(int16_t *mono_pcm, size_t capacity_samples,
                                              size_t *captured_samples, uint32_t max_duration_ms)
{
    if (!audio_ready || !mono_pcm || !captured_samples || capacity_samples < AUDIO_FRAME_SAMPLES) return ESP_ERR_INVALID_ARG;
    *captured_samples = 0;
    int16_t stereo[AUDIO_FRAME_SAMPLES * 2];
    uint32_t active_frames = 0, silent_frames = 0;
    bool started = false;
    const size_t max_samples = capacity_samples;
    const uint32_t max_frames = (max_duration_ms ? max_duration_ms : AUDIO_MAX_UTTERANCE_MS) / 20U;

    for (uint32_t frame = 0; frame < max_frames && *captured_samples + AUDIO_FRAME_SAMPLES <= max_samples; ++frame) {
        if (playback_active) { vTaskDelay(pdMS_TO_TICKS(20)); continue; }
        esp_err_t err = esp_codec_dev_read(microphone, stereo, sizeof(stereo));
        if (err != ESP_OK) return err;
        float energy = 0.0f;
        for (size_t i = 0; i < AUDIO_FRAME_SAMPLES; ++i) {
            const int32_t mixed = ((int32_t)stereo[i * 2] + (int32_t)stereo[i * 2 + 1]) / 2;
            mono_pcm[*captured_samples + i] = (int16_t)mixed;
            energy += (float)(mixed * mixed);
        }
        const float rms = sqrtf(energy / AUDIO_FRAME_SAMPLES);
        if (rms > VAD_RMS_THRESHOLD) {
            active_frames++;
            silent_frames = 0;
            if (active_frames >= VAD_START_FRAMES) started = true;
        } else if (started) {
            silent_frames++;
        } else {
            active_frames = 0;
        }
        if (started) *captured_samples += AUDIO_FRAME_SAMPLES;
        if (started && silent_frames >= VAD_END_SILENCE_FRAMES) break;
    }
    if (!started || *captured_samples < AUDIO_RATE / 3) {
        *captured_samples = 0;
        return ESP_ERR_NOT_FOUND;
    }
    return ESP_OK;
}
