#include "companion_emotion.h"

#include <string.h>

static float clamp01(float value)
{
    if (value < 0.0f) return 0.0f;
    if (value > 100.0f) return 100.0f;
    return value;
}

void companion_emotion_init(companion_state_t *state)
{
    if (!state) return;
    *state = (companion_state_t) {
        .energy = 82.0f, .comfort = 72.0f, .social = 42.0f,
        .curiosity = 38.0f, .trust = 25.0f, .playfulness = 35.0f, .temperament = 0, .initiative = 0, .quirk = 0
    };
}

void companion_emotion_tick(companion_state_t *state, uint32_t elapsed_seconds, bool is_sleeping)
{
    if (!state) return;
    const float dt = (float)elapsed_seconds;
    if (is_sleeping) {
        state->energy = clamp01(state->energy + dt * 0.55f);
        state->social = clamp01(state->social + dt * 0.008f);
        state->comfort = clamp01(state->comfort + dt * 0.08f);
        return;
    }
    state->energy = clamp01(state->energy - dt * 0.018f);
    state->curiosity = clamp01(state->curiosity + dt * 0.025f);
    state->social = clamp01(state->social + dt * 0.018f);
    state->comfort = clamp01(state->comfort - dt * 0.006f);
}

void companion_emotion_interaction(companion_state_t *state, const char *kind)
{
    if (!state || !kind) return;
    state->interactions++;
    if (strcmp(kind, "pet") == 0) {
        state->comfort = clamp01(state->comfort + 11.0f);
        state->social = clamp01(state->social - 8.0f);
        state->trust = clamp01(state->trust + 1.8f);
    } else if (strcmp(kind, "play") == 0) {
        state->energy = clamp01(state->energy - 7.0f);
        state->curiosity = clamp01(state->curiosity - 14.0f);
        state->social = clamp01(state->social - 9.0f);
        state->playfulness = clamp01(state->playfulness + 2.0f);
    } else if (strcmp(kind, "call") == 0) {
        state->social = clamp01(state->social - 7.0f);
        state->curiosity = clamp01(state->curiosity - 6.0f);
        state->trust = clamp01(state->trust + 0.8f);
    } else if (strcmp(kind, "feed") == 0) {
        state->energy = clamp01(state->energy + 28.0f);
        state->comfort = clamp01(state->comfort + 12.0f);
        state->playfulness = clamp01(state->playfulness + 4.0f);
        state->social = clamp01(state->social + 1.5f);
    } else if (strcmp(kind, "clean") == 0) {
        state->comfort = clamp01(state->comfort + 16.0f);
        state->social = clamp01(state->social - 3.0f);
    } else if (strcmp(kind, "rest") == 0) {
        state->energy = clamp01(state->energy + 4.0f);
        state->comfort = clamp01(state->comfort + 2.0f);
    }
}

companion_emotion_t companion_emotion_choose(const companion_state_t *s,
                                              uint8_t hour,
                                              uint32_t random_value,
                                              companion_emotion_t previous)
{
    if (!s) return COMPANION_CALM;
    companion_emotion_t chosen;
    if (s->energy < 13.0f || ((hour >= 23 || hour < 6) && s->energy < 48.0f)) chosen = COMPANION_SLEEPY;
    else if (s->comfort < 18.0f) chosen = COMPANION_SAD;
    else if (s->social > 88.0f && s->trust > 35.0f) chosen = COMPANION_AFFECTIONATE;
    else if (s->quirk == 1 && s->energy < 58.0f) chosen = COMPANION_SLEEPY;
    else if (s->quirk == 2 && s->curiosity > 52.0f) chosen = COMPANION_CURIOUS;
    else if (s->quirk == 4 && s->social > 58.0f) chosen = COMPANION_SHY;
    else if (s->curiosity > 88.0f) chosen = COMPANION_CURIOUS;
    else if (s->energy < 28.0f) chosen = COMPANION_CONCERNED;
    else {
        const uint32_t roll = random_value % 100U;
        if (s->quirk == 3 && roll < 55U) {
            chosen = s->playfulness > 50.0f ? COMPANION_EXCITED : COMPANION_HAPPY;
        } else if (s->temperament == 1) {
            if (roll < 62U) chosen = COMPANION_CALM;
            else if (roll < 82U) chosen = COMPANION_CURIOUS;
            else chosen = COMPANION_HAPPY;
        } else if (s->temperament == 2) {
            if (roll < 34U) chosen = COMPANION_EXCITED;
            else if (roll < 68U) chosen = COMPANION_HAPPY;
            else if (roll < 88U) chosen = COMPANION_CURIOUS;
            else chosen = COMPANION_SURPRISED;
        } else if (s->temperament == 3) {
            if (roll < 55U) chosen = COMPANION_SHY;
            else if (roll < 80U) chosen = COMPANION_CALM;
            else chosen = COMPANION_CURIOUS;
        } else if (s->temperament == 4) {
            if (roll < 45U) chosen = COMPANION_CURIOUS;
            else if (roll < 80U) chosen = COMPANION_CALM;
            else chosen = COMPANION_SLEEPY;
        } else {
            if (roll < 34U) chosen = COMPANION_CALM;
            else if (roll < 54U) chosen = COMPANION_CURIOUS;
            else if (roll < 70U) chosen = s->playfulness > 55.0f ? COMPANION_EXCITED : COMPANION_HAPPY;
            else if (roll < 82U) chosen = COMPANION_SHY;
            else if (roll < 91U) chosen = COMPANION_AFFECTIONATE;
            else chosen = COMPANION_SURPRISED;
        }
    }
    /* Do not bounce between the same two faces on every autonomous tick. */
    if (chosen == previous && chosen != COMPANION_SLEEPY) return COMPANION_CALM;
    return chosen;
}

const char *companion_emotion_name(companion_emotion_t emotion)
{
    static const char *names[COMPANION_EMOTION_COUNT] = {
        "спокойствие", "радость", "любопытство", "сонливость", "грусть",
        "нежность", "восторг", "удивление", "беспокойство", "смущение", "головокружение", "зажмуривание"
    };
    return emotion < COMPANION_EMOTION_COUNT ? names[emotion] : names[COMPANION_CALM];
}
