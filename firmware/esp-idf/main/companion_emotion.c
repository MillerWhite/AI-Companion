#include "companion_emotion.h"

#include <string.h>

static float clamp100(float value)
{
    if (value < 0.0f) return 0.0f;
    if (value > 100.0f) return 100.0f;
    return value;
}

static uint32_t bounded_roll(uint32_t value) { return value % 100U; }

void companion_emotion_init(companion_state_t *state)
{
    if (!state) return;
    *state = (companion_state_t) {
        .curiosity = 38.0f,
        .trust = 25.0f,
        .playfulness = 35.0f,
        .calmness = 35.0f,
        .responsiveness = 30.0f,
        .irritation = 0.0f,
        .temperament = 0,
        .initiative = 0,
        .quirk = 0
    };
}

void companion_emotion_tick(companion_state_t *state, uint32_t elapsed_seconds, bool is_sleeping)
{
    /* Deliberately no time-based deterioration or forced care loop. */
    (void)state;
    (void)elapsed_seconds;
    (void)is_sleeping;
}

void companion_emotion_set_personality(companion_state_t *state,
                                       uint8_t temperament,
                                       uint8_t initiative,
                                       uint8_t quirk)
{
    if (!state) return;
    state->temperament = temperament <= 4 ? temperament : 0;
    state->initiative = initiative <= 2 ? initiative : 0;
    state->quirk = quirk <= 4 ? quirk : 0;
}

void companion_emotion_interaction(companion_state_t *state, const char *kind)
{
    if (!state || !kind) return;
    state->interactions++;
    if (!strcmp(kind, "pet") || !strcmp(kind, "long_pet")) {
        const bool long_pet = !strcmp(kind, "long_pet");
        state->pet_count++;
        state->trust = clamp100(state->trust + (long_pet ? 3.0f : 1.8f));
        state->irritation = clamp100(state->irritation - 4.0f);
        state->calmness = clamp100(state->calmness + (long_pet ? 1.4f : 0.7f));
        state->playfulness = clamp100(state->playfulness + 0.2f);
    } else if (!strcmp(kind, "call")) {
        state->call_count++;
        state->trust = clamp100(state->trust + 1.0f);
        state->curiosity = clamp100(state->curiosity + 2.5f);
        state->responsiveness = clamp100(state->responsiveness + 1.7f);
        state->irritation = clamp100(state->irritation - 2.0f);
    } else if (!strcmp(kind, "charge")) {
        /* Power is environmental context, not a consumable need. */
        state->trust = clamp100(state->trust + 0.2f);
    } else if (!strcmp(kind, "shake")) {
        state->curiosity = clamp100(state->curiosity + 2.0f);
        state->surprise_count++;
    } else if (!strcmp(kind, "surprise")) {
        state->surprise_count++;
        state->curiosity = clamp100(state->curiosity + 1.5f);
    } else if (!strcmp(kind, "rest")) {
        state->calmness = clamp100(state->calmness + 0.8f);
        state->irritation = clamp100(state->irritation - 1.5f);
    } else if (!strcmp(kind, "frustration")) {
        state->irritation = clamp100(state->irritation + 18.0f);
        state->surprise_count++;
    }
}

uint8_t companion_emotion_initiative_chance(const companion_state_t *state)
{
    if (!state) return 0;
    uint8_t base = state->initiative == 1 ? 42U : (state->initiative == 2 ? 12U : 25U);
    /* Learned responsiveness and energetic temperament subtly tune initiative. */
    int adjustment = ((int)state->responsiveness - 30) / 8;
    if (state->temperament == 2) adjustment += 5;
    if (state->temperament == 3) adjustment -= 4;
    int chance = (int)base + adjustment;
    if (chance < 5) chance = 5;
    if (chance > 55) chance = 55;
    return (uint8_t)chance;
}

static companion_emotion_t choose_weighted(const companion_state_t *s,
                                            uint8_t hour,
                                            uint32_t roll)
{
    /* Weight table is deterministic and can be exercised in host-side tests. */
    uint8_t calm = 25, curious = 22, happy = 18, shy = 10, sleepy = 5, affectionate = 8, excited = 7, surprised = 5, angry = (uint8_t)(2.0f + s->irritation / 5.0f);
    if (s->temperament == 1) { calm += 22; happy -= 5; excited -= 5; }
    else if (s->temperament == 2) { excited += 15; happy += 10; calm -= 12; }
    else if (s->temperament == 3) { shy += 20; calm += 5; excited -= 5; }
    else if (s->temperament == 4) { curious += 12; sleepy += 7; calm += 5; }
    if (s->quirk == 1 && (hour >= 22 || hour < 7)) sleepy += 22;
    if (s->quirk == 2) curious += 18;
    if (s->quirk == 3) excited += 18;
    if (s->quirk == 4) shy += 18;
    if (s->trust > 60.0f) affectionate += 12;
    if (s->curiosity > 65.0f) curious += 10;
    if (s->playfulness > 65.0f) { excited += 10; happy += 5; }
    if (s->calmness > 70.0f) calm += 8;
    if (s->responsiveness > 65.0f) happy += 5;

    int weights[9] = {calm, curious, happy, shy, sleepy, affectionate, excited, surprised, angry};
    int total = 0;
    for (size_t i = 0; i < 9; ++i) { if (weights[i] < 1) weights[i] = 1; total += weights[i]; }
    int pick = (int)(roll % (uint32_t)total);
    for (size_t i = 0; i < 8; ++i) {
        if (pick < weights[i]) {
            static const companion_emotion_t map[9] = {
                COMPANION_CALM, COMPANION_CURIOUS, COMPANION_HAPPY, COMPANION_SHY,
                COMPANION_SLEEPY, COMPANION_AFFECTIONATE, COMPANION_EXCITED, COMPANION_SURPRISED, COMPANION_ANGRY
            };
            return map[i];
        }
        pick -= weights[i];
    }
    return COMPANION_CALM;
}

companion_emotion_t companion_emotion_choose(const companion_state_t *s,
                                              uint8_t hour,
                                              uint32_t random_value,
                                              companion_emotion_t previous)
{
    if (!s) return COMPANION_CALM;
    companion_emotion_t chosen = choose_weighted(s, hour, bounded_roll(random_value));
    /* Never repeat an autonomous face change; choose a stable neutral pause instead. */
    if (chosen == previous) {
        if (previous != COMPANION_CALM) return COMPANION_CALM;
        return COMPANION_CURIOUS;
    }
    return chosen;
}

const char *companion_emotion_name(companion_emotion_t emotion)
{
    static const char *names[COMPANION_EMOTION_COUNT] = {
        "спокойствие", "радость", "любопытство", "сонливость", "грусть",
        "нежность", "восторг", "удивление", "испуг", "беспокойство", "смущение", "головокружение", "зажмуривание", "злость"
    };
    return emotion < COMPANION_EMOTION_COUNT ? names[emotion] : names[COMPANION_CALM];
}
