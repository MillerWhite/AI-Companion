#include "companion_emotion.h"

#include <string.h>

static float clamp100(float value)
{
    if (value < 0.0f) return 0.0f;
    if (value > 100.0f) return 100.0f;
    return value;
}

void companion_emotion_init(companion_state_t *state)
{
    if (!state) return;
    *state = (companion_state_t) {
        .curiosity = 38.0f,
        .trust = 25.0f,
        .playfulness = 35.0f,
        .temperament = 0,
        .initiative = 0,
        .quirk = 0
    };
}

/* Kept for ABI/source compatibility with the main loop. Time alone never worsens mood. */
void companion_emotion_tick(companion_state_t *state, uint32_t elapsed_seconds, bool is_sleeping)
{
    (void)state;
    (void)elapsed_seconds;
    (void)is_sleeping;
}

void companion_emotion_interaction(companion_state_t *state, const char *kind)
{
    if (!state || !kind) return;
    state->interactions++;
    if (!strcmp(kind, "pet")) {
        state->trust = clamp100(state->trust + 2.0f);
        state->curiosity = clamp100(state->curiosity + 3.0f);
    } else if (!strcmp(kind, "play")) {
        state->playfulness = clamp100(state->playfulness + 5.0f);
        state->curiosity = clamp100(state->curiosity + 4.0f);
    } else if (!strcmp(kind, "call")) {
        state->trust = clamp100(state->trust + 1.0f);
        state->curiosity = clamp100(state->curiosity + 5.0f);
    } else if (!strcmp(kind, "charge")) {
        /* Charging is a contextual event, never feeding or refilling a need meter. */
        state->trust = clamp100(state->trust + 0.5f);
    } else if (!strcmp(kind, "shake")) {
        state->curiosity = clamp100(state->curiosity + 2.0f);
    }
}

companion_emotion_t companion_emotion_choose(const companion_state_t *s,
                                              uint8_t hour,
                                              uint32_t random_value,
                                              companion_emotion_t previous)
{
    if (!s) return COMPANION_CALM;
    const uint32_t roll = random_value % 100U;
    companion_emotion_t chosen = COMPANION_CALM;

    if ((hour >= 23 || hour < 6) && s->quirk == 1 && roll < 42U) {
        chosen = COMPANION_SLEEPY;
    } else if (s->quirk == 2 && roll < 48U) {
        chosen = COMPANION_CURIOUS;
    } else if (s->quirk == 3 && roll < 52U) {
        chosen = s->playfulness >= 50.0f ? COMPANION_EXCITED : COMPANION_HAPPY;
    } else if (s->quirk == 4 && roll < 48U) {
        chosen = COMPANION_SHY;
    } else if (s->temperament == 1) {
        chosen = roll < 72U ? COMPANION_CALM : (roll < 88U ? COMPANION_CURIOUS : COMPANION_HAPPY);
    } else if (s->temperament == 2) {
        chosen = roll < 35U ? COMPANION_EXCITED : (roll < 70U ? COMPANION_HAPPY :
                  (roll < 90U ? COMPANION_CURIOUS : COMPANION_SURPRISED));
    } else if (s->temperament == 3) {
        chosen = roll < 56U ? COMPANION_SHY : (roll < 82U ? COMPANION_CALM : COMPANION_CURIOUS);
    } else if (s->temperament == 4) {
        chosen = roll < 48U ? COMPANION_CURIOUS : (roll < 82U ? COMPANION_CALM : COMPANION_SLEEPY);
    } else {
        if (roll < 34U) chosen = COMPANION_CALM;
        else if (roll < 55U) chosen = COMPANION_CURIOUS;
        else if (roll < 72U) chosen = s->playfulness > 50.0f ? COMPANION_EXCITED : COMPANION_HAPPY;
        else if (roll < 84U) chosen = COMPANION_SHY;
        else if (roll < 94U && s->trust > 25.0f) chosen = COMPANION_AFFECTIONATE;
        else chosen = COMPANION_SURPRISED;
    }

    /* Avoid rapid face flicker while keeping autonomous behavior alive. */
    if (chosen == previous) return COMPANION_CALM;
    return chosen;
}

const char *companion_emotion_name(companion_emotion_t emotion)
{
    static const char *names[COMPANION_EMOTION_COUNT] = {
        "спокойствие", "радость", "любопытство", "сонливость", "грусть",
        "нежность", "восторг", "удивление", "испуг", "беспокойство", "смущение", "головокружение", "зажмуривание"
    };
    return emotion < COMPANION_EMOTION_COUNT ? names[emotion] : names[COMPANION_CALM];
}
