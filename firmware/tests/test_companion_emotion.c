#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "companion_emotion.h"

static void test_defaults(void)
{
    companion_state_t state;
    companion_emotion_init(&state);
    assert(state.curiosity == 38.0f);
    assert(state.trust == 25.0f);
    assert(state.playfulness == 35.0f);
    assert(state.calmness == 35.0f);
    assert(state.responsiveness == 30.0f);
    assert(state.irritation == 0.0f);
    assert(companion_emotion_initiative_chance(&state) == 25);
}

static void test_relationship_is_event_driven(void)
{
    companion_state_t state;
    companion_emotion_init(&state);
    companion_emotion_interaction(&state, "pet");
    assert(state.pet_count == 1);
    assert(state.interactions == 1);
    assert(state.trust > 25.0f);
    float trust = state.trust;
    companion_emotion_tick(&state, 86400U, false);
    assert(state.trust == trust);
    companion_emotion_interaction(&state, "long_pet");
    assert(state.pet_count == 2);
    assert(state.trust > trust);
}

static void test_personality_and_initiative_bounds(void)
{
    companion_state_t state;
    companion_emotion_init(&state);
    companion_emotion_set_personality(&state, 2, 1, 3);
    assert(state.temperament == 2);
    assert(state.initiative == 1);
    assert(state.quirk == 3);
    uint8_t chance = companion_emotion_initiative_chance(&state);
    assert(chance >= 5 && chance <= 55);
    companion_emotion_set_personality(&state, 255, 255, 255);
    assert(state.temperament == 0 && state.initiative == 0 && state.quirk == 0);
}

static void test_frustration_is_event_driven_and_angry_is_named(void)
{
    companion_state_t state;
    companion_emotion_init(&state);
    companion_emotion_interaction(&state, "frustration");
    assert(state.irritation == 18.0f);
    assert(strcmp(companion_emotion_name(COMPANION_ANGRY), "злость") == 0);
    float irritation = state.irritation;
    companion_emotion_tick(&state, 86400U, false);
    assert(state.irritation == irritation);
    companion_emotion_interaction(&state, "pet");
    assert(state.irritation < irritation);

    state.irritation = 100.0f;
    unsigned angry_count = 0;
    for (uint32_t roll = 0; roll < 1000; ++roll) {
        if (companion_emotion_choose(&state, 12, roll, COMPANION_CALM) == COMPANION_ANGRY) angry_count++;
    }
    assert(angry_count > 0);
}

static void test_emotion_selection_is_valid(void)
{
    companion_state_t state;
    companion_emotion_init(&state);
    for (uint32_t roll = 0; roll < 1000; ++roll) {
        companion_emotion_t emotion = companion_emotion_choose(&state, 12, roll, COMPANION_CALM);
        assert(emotion < COMPANION_EMOTION_COUNT);
    }
}

int main(void)
{
    test_defaults();
    test_relationship_is_event_driven();
    test_personality_and_initiative_bounds();
    test_emotion_selection_is_valid();
    test_frustration_is_event_driven_and_angry_is_named();
    puts("companion emotion core: all tests passed");
    return 0;
}
