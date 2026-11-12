#include "companion_audio.h"

#include <math.h>
#include "companion_voice.h"
#include "esp_codec_dev.h"
#include "esp_log.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "bsp/esp-bsp.h"

#define AUDIO_RATE 16000
#define AUDIO_FRAME_SAMPLES 320
#define AUDIO_COOLDOWN_US 5200000LL
#define VAD_RMS_THRESHOLD 460.0f
#define VAD_END_SILENCE_FRAMES 32
#define VAD_START_FRAMES 3
#define PLAYBACK_CAPACITY 24000
#define PLAYBACK_CHUNK 256

static const char *TAG = "companion_audio";
static esp_codec_dev_handle_t speaker;
static esp_codec_dev_handle_t microphone;
static SemaphoreHandle_t audio_mutex;
static bool audio_ready;
static int64_t last_sound_us;
static volatile bool playback_active;
static uint8_t sound_temperament;
static int16_t playback_pcm[PLAYBACK_CAPACITY];

bool companion_audio_is_ready(void) { return audio_ready; }

void companion_audio_set_temperament(uint8_t temperament)
{
    sound_temperament = temperament <= 4 ? temperament : 0;
    static const float volumes[] = {0.24f, 0.22f, 0.27f, 0.20f, 0.22f};
    voice_set_volume(volumes[sound_temperament]);
}

esp_err_t companion_audio_init(void)
{
    if (audio_ready) return ESP_OK;
    speaker = bsp_audio_codec_speaker_init();
    microphone = bsp_audio_codec_microphone_init();
    if (!speaker || !microphone) return ESP_FAIL;

    const esp_codec_dev_sample_info_t format = {
        .sample_rate = AUDIO_RATE,
        .channel = 1,
        .bits_per_sample = 16,
    };
    /* Follow the Waveshare 1.75C reference sequence: the BSP owns I2S and
       codec pins; open each fresh codec handle at the mono 16 kHz voice rate. */
    ESP_RETURN_ON_ERROR(esp_codec_dev_open(speaker, &format), TAG, "speaker codec open failed");
    ESP_RETURN_ON_ERROR(esp_codec_dev_set_out_vol(speaker, 24), TAG, "speaker volume setup failed");
    ESP_RETURN_ON_ERROR(esp_codec_dev_open(microphone, &format), TAG, "microphone codec open failed");
    ESP_RETURN_ON_ERROR(esp_codec_dev_set_in_gain(microphone, 18.0f), TAG, "microphone gain setup failed");

    audio_mutex = xSemaphoreCreateMutex();
    if (!audio_mutex) return ESP_ERR_NO_MEM;
    voice_init(AUDIO_RATE);
    voice_set_volume(0.24f);
    audio_ready = true;
    ESP_LOGI(TAG, "ES8311 speaker and ES7210 microphone initialized at 16 kHz mono");
    return ESP_OK;
}

static int voice_for_emotion(companion_emotion_t emotion)
{
    switch (emotion) {
        case COMPANION_HAPPY: return VOICE_HAPPY;
        case COMPANION_CURIOUS: return VOICE_HM;
        case COMPANION_SLEEPY: return VOICE_YAWN;
        case COMPANION_SAD: return VOICE_SAD;
        case COMPANION_AFFECTIONATE: return VOICE_LOVE;
        case COMPANION_EXCITED: return VOICE_HAPPY;
        case COMPANION_SURPRISED: return VOICE_OH;
        case COMPANION_CONCERNED: return VOICE_HM;
        case COMPANION_SHY: return VOICE_POKE;
        case COMPANION_CALM:
        default: return VOICE_HM;
    }
}

esp_err_t companion_audio_play(companion_emotion_t emotion, bool force)
{
    if (!audio_ready || !speaker || !audio_mutex) return ESP_ERR_INVALID_STATE;
    if (xSemaphoreTake(audio_mutex, pdMS_TO_TICKS(250)) != pdTRUE) return ESP_ERR_TIMEOUT;

    const int64_t now = esp_timer_get_time();
    if (!force && now - last_sound_us < AUDIO_COOLDOWN_US) {
        xSemaphoreGive(audio_mutex);
        return ESP_OK;
    }
    if ((unsigned)emotion >= COMPANION_EMOTION_COUNT) emotion = COMPANION_CALM;
    last_sound_us = now;
    playback_active = true;
    voice_say(voice_for_emotion(emotion));

    size_t samples = 0;
    while (voice_busy() && samples + PLAYBACK_CHUNK <= PLAYBACK_CAPACITY) {
        voice_render(&playback_pcm[samples], PLAYBACK_CHUNK);
        samples += PLAYBACK_CHUNK;
    }

    esp_err_t err = samples ? esp_codec_dev_write(speaker, playback_pcm, (int)(samples * sizeof(int16_t))) : ESP_OK;
    playback_active = false;
    xSemaphoreGive(audio_mutex);
    if (err != ESP_OK) ESP_LOGW(TAG, "Nonverbal sound playback failed: %s", esp_err_to_name(err));
    return err;
}

esp_err_t companion_audio_capture_utterance(int16_t *mono_pcm, size_t capacity_samples,
                                             size_t *captured_samples, uint32_t max_duration_ms)
{
    if (!audio_ready || !mono_pcm || !captured_samples || capacity_samples < AUDIO_FRAME_SAMPLES) return ESP_ERR_INVALID_ARG;
    *captured_samples = 0;
    int16_t frame_pcm[AUDIO_FRAME_SAMPLES];
    uint32_t active_frames = 0, silent_frames = 0;
    bool started = false;
    const uint32_t max_frames = (max_duration_ms ? max_duration_ms : 6500U) / 20U;

    for (uint32_t frame = 0; frame < max_frames && *captured_samples + AUDIO_FRAME_SAMPLES <= capacity_samples; ++frame) {
        if (xSemaphoreTake(audio_mutex, pdMS_TO_TICKS(80)) != pdTRUE) continue;
        if (playback_active) {
            xSemaphoreGive(audio_mutex);
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        esp_err_t err = esp_codec_dev_read(microphone, frame_pcm, sizeof(frame_pcm));
        xSemaphoreGive(audio_mutex);
        if (err != ESP_OK) return err;

        float energy = 0.0f;
        for (size_t i = 0; i < AUDIO_FRAME_SAMPLES; ++i) {
            const float sample = (float)frame_pcm[i];
            energy += sample * sample;
        }
        const float rms = sqrtf(energy / (float)AUDIO_FRAME_SAMPLES);
        if (rms > VAD_RMS_THRESHOLD) {
            ++active_frames;
            silent_frames = 0;
            if (active_frames >= VAD_START_FRAMES) started = true;
        } else if (started) {
            ++silent_frames;
        } else {
            active_frames = 0;
        }

        if (started) {
            memcpy(mono_pcm + *captured_samples, frame_pcm, sizeof(frame_pcm));
            *captured_samples += AUDIO_FRAME_SAMPLES;
        }
        if (started && silent_frames >= VAD_END_SILENCE_FRAMES) break;
    }

    if (!started || *captured_samples < AUDIO_RATE / 3) {
        *captured_samples = 0;
        return ESP_ERR_NOT_FOUND;
    }
    return ESP_OK;
}
