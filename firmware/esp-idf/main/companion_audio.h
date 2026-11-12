#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "companion_emotion.h"

esp_err_t companion_audio_init(void);
esp_err_t companion_audio_play(companion_emotion_t emotion, bool force);
esp_err_t companion_audio_capture_utterance(int16_t *mono_pcm, size_t capacity_samples,
                                              size_t *captured_samples, uint32_t max_duration_ms);
bool companion_audio_is_ready(void);
void companion_audio_set_temperament(uint8_t temperament);
