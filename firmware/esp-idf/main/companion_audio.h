#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "companion_emotion.h"
#include "companion_voice.h"

enum { COMPANION_AUDIO_BLOW = 1U, COMPANION_AUDIO_CLAP = 2U };

esp_err_t companion_audio_init(void);
esp_err_t companion_audio_play(companion_emotion_t emotion, bool force);
esp_err_t companion_audio_capture_utterance(int16_t *mono_pcm, size_t capacity_samples,
                                              size_t *captured_samples, uint32_t max_duration_ms);
bool companion_audio_is_ready(void);
uint32_t companion_audio_take_events(void);
esp_err_t companion_audio_play_sound(int sound, bool force);
void companion_audio_set_temperament(uint8_t temperament);
