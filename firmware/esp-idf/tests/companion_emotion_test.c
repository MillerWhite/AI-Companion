#include "companion_emotion.h"

#include <assert.h>
#include <stdio.h>

static void test_initial_state(void)
{
    companion_state_t state;
    companion_emotion_init(&state);
    assert(state.energy == 82.0f);
    assert(state.comfort == 72.0f);
    assert(state.trust == 25.0f);
    assert(companion_emotion_choose(&state, 12, 0, COMPANION_SURPRISED) == COMPANION_CALM);
}

static void test_needs_and_sleep_recovery(void)
{
    companion_state_t state;
    companion_emotion_init(&state);
    const float energy = state.energy;
    companion_emotion_tick(&state, 1000, false);
    assert(state.energy < energy);
    const float tired = state.energy;
    companion_emotion_tick(&state, 100, true);
    assert(state.energy > tired);
    assert(state.energy <= 100.0f);
}

static void test_interactions_change_relationship(void)
{
    companion_state_t state;
    companion_emotion_init(&state);
    const float comfort = state.comfort;
    const float trust = state.trust;
    companion_emotion_interaction(&state, "pet");
    assert(state.comfort > comfort);
    assert(state.trust > trust);
    const float energy = state.energy;
    const float playful = state.playfulness;
    companion_emotion_interaction(&state, "play");
    assert(state.energy < energy);
    assert(state.playfulness > playful);
    assert(state.interactions == 2);
}

static void test_sadness_is_need_driven(void)
{
    companion_state_t state;
    companion_emotion_init(&state);
    state.comfort = 10.0f;
    assert(companion_emotion_choose(&state, 14, 17, COMPANION_CALM) == COMPANION_SAD);
}

static void test_sleep_is_need_and_time_driven(void)
{
    companion_state_t state;
    companion_emotion_init(&state);
    state.energy = 40.0f;
    assert(companion_emotion_choose(&state, 23, 12, COMPANION_CALM) == COMPANION_SLEEPY);
    state.energy = 8.0f;
    assert(companion_emotion_choose(&state, 12, 12, COMPANION_CALM) == COMPANION_SLEEPY);
}

static void test_personality_changes_expression(void)
{
    companion_state_t state;
    companion_emotion_init(&state);
    state.temperament = 3; /* shy */
    assert(companion_emotion_choose(&state, 12, 10, COMPANION_HAPPY) == COMPANION_SHY);
    state.temperament = 2; /* energetic */
    assert(companion_emotion_choose(&state, 12, 10, COMPANION_CALM) == COMPANION_EXCITED);
}

static void test_needs_are_clamped(void)
{
    companion_state_t state;
    companion_emotion_init(&state);
    companion_emotion_tick(&state, UINT32_MAX, false);
    assert(state.energy >= 0.0f && state.energy <= 100.0f);
    assert(state.social >= 0.0f && state.social <= 100.0f);
    assert(state.curiosity >= 0.0f && state.curiosity <= 100.0f);
    companion_emotion_tick(&state, UINT32_MAX, true);
    assert(state.energy >= 0.0f && state.energy <= 100.0f);
}

int main(void)
{
    test_initial_state();
    test_needs_and_sleep_recovery();
    test_interactions_change_relationship();
    test_sadness_is_need_driven();
    test_sleep_is_need_and_time_driven();
    test_personality_changes_expression();
    test_needs_are_clamped();
    puts("companion emotion core: all checks passed");
    return 0;
}
