#include "companion_audio.h"

#include <math.h>
#include <string.h>
#include "esp_timer.h"
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
static volatile uint32_t detected_events;
static uint8_t blow_frames;
static int64_t last_blow_event_us;
static int64_t last_clap_event_us;
static int16_t playback_pcm[PLAYBACK_CAPACITY];

bool companion_audio_is_ready(void) { return audio_ready; }

uint32_t companion_audio_take_events(void)
{
    uint32_t events = detected_events;
    detected_events = 0;
    return events;
}

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
        case COMPANION_CURIOUS: return VOICE_TICK;
        case COMPANION_SLEEPY: return VOICE_YAWN;
        case COMPANION_SAD: return VOICE_SAD;
        case COMPANION_AFFECTIONATE: return VOICE_LOVE;
        case COMPANION_EXCITED: return VOICE_HAPPY;
        case COMPANION_SURPRISED: return VOICE_OH;
        case COMPANION_CONCERNED: return VOICE_GROWL;
        case COMPANION_SHY: return VOICE_POKE;
        case COMPANION_DIZZY: return VOICE_DIZZY;
        case COMPANION_FLINCH: return VOICE_POKE;
        case COMPANION_CALM:
        default: return VOICE_HM;
    }
}

static esp_err_t audio_play_voice(int sound, bool force)
{
    if (!audio_ready || !speaker || !audio_mutex) return ESP_ERR_INVALID_STATE;
    if (xSemaphoreTake(audio_mutex, pdMS_TO_TICKS(250)) != pdTRUE) return ESP_ERR_TIMEOUT;
    const int64_t now = esp_timer_get_time();
    if (!force && now - last_sound_us < AUDIO_COOLDOWN_US) {
        xSemaphoreGive(audio_mutex);
        return ESP_OK;
    }
    if (sound < 0 || sound >= VOICE_COUNT) sound = VOICE_HM;
    last_sound_us = now;
    playback_active = true;
    voice_say(sound);
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

esp_err_t companion_audio_play(companion_emotion_t emotion, bool force)
{
    if ((unsigned)emotion >= COMPANION_EMOTION_COUNT) emotion = COMPANION_CALM;
    return audio_play_voice(voice_for_emotion(emotion), force);
}

esp_err_t companion_audio_play_sound(int sound, bool force)
{
    return audio_play_voice(sound, force);
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

        float energy = 0.0f, peak = 0.0f, mean = 0.0f;
        for (size_t i = 0; i < AUDIO_FRAME_SAMPLES; ++i) mean += (float)frame_pcm[i];
        mean /= (float)AUDIO_FRAME_SAMPLES;
        unsigned zero_crossings = 0;
        float diff_energy = 0.0f;
        float previous = (float)frame_pcm[0] - mean;
        for (size_t i = 0; i < AUDIO_FRAME_SAMPLES; ++i) {
            const float sample = (float)frame_pcm[i] - mean;
            energy += sample * sample;
            if (fabsf(sample) > peak) peak = fabsf(sample);
            if (i > 0) {
                if ((sample >= 0.0f && previous < 0.0f) || (sample < 0.0f && previous >= 0.0f)) ++zero_crossings;
                const float diff = sample - previous;
                diff_energy += diff * diff;
            }
            previous = sample;
        }
        const float rms = sqrtf(energy / (float)AUDIO_FRAME_SAMPLES);
        const float zcr = (float)zero_crossings / (float)AUDIO_FRAME_SAMPLES;
        const float high_frequency_ratio = sqrtf(diff_energy / (float)AUDIO_FRAME_SAMPLES) / (rms + 1.0f);
        const int64_t now_us = esp_timer_get_time();

        /* A clap is a short high-crest impulse; wind is sustained and low-frequency. */
        if (peak > 10500.0f && rms > 1700.0f && peak > rms * 3.0f &&
            now_us - last_clap_event_us > 1300000LL) {
            last_clap_event_us = now_us;
            detected_events |= COMPANION_AUDIO_CLAP;
            *captured_samples = 0;
            blow_frames = 0;
            return ESP_ERR_NOT_FOUND;
        }
        const bool blow_candidate = rms > 650.0f && rms < 6500.0f &&
                                    zcr < 0.13f && high_frequency_ratio < 1.05f &&
                                    peak > 1400.0f;
        if (blow_candidate) {
            if (blow_frames < 20) ++blow_frames;
        } else if (blow_frames > 0) {
            --blow_frames;
        }
        if (blow_frames >= 8 && now_us - last_blow_event_us > 2600000LL) {
            last_blow_event_us = now_us;
            detected_events |= COMPANION_AUDIO_BLOW;
            *captured_samples = 0;
            blow_frames = 0;
            return ESP_ERR_NOT_FOUND;
        }

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
