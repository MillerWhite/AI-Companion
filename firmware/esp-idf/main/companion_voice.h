/* Derived from Pip-KeyPet ui/voice.h by David Novikov (MIT License).
 * Procedural nonverbal vocalizations; no speech or recorded audio.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>

enum {
    VOICE_POKE, VOICE_HAPPY, VOICE_SAD, VOICE_GROWL, VOICE_OH, VOICE_LOVE,
    VOICE_PURR, VOICE_NOM, VOICE_BURP, VOICE_YAWN, VOICE_DIZZY, VOICE_DERP,
    VOICE_HM, VOICE_CENSOR, VOICE_ALARM, VOICE_HMPH, VOICE_TICK, VOICE_COUNT
};

void voice_init(int sample_rate);
void voice_say(int sound);
void voice_set_volume(float volume);
float voice_volume(void);
bool voice_busy(void);
bool voice_render(int16_t *out, int count);
const char *voice_name(int sound);
