#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    COMPANION_CALM = 0,
    COMPANION_HAPPY,
    COMPANION_CURIOUS,
    COMPANION_SLEEPY,
    COMPANION_SAD,
    COMPANION_AFFECTIONATE,
    COMPANION_EXCITED,
    COMPANION_SURPRISED,
    COMPANION_SCARED,
    COMPANION_CONCERNED,
    COMPANION_SHY,
    COMPANION_DIZZY,
    COMPANION_FLINCH,
    COMPANION_EMOTION_COUNT
} companion_emotion_t;

typedef struct {
    float energy;
    float hunger;
    float comfort;
    float social;
    float curiosity;
    float trust;
    float playfulness;
    uint32_t interactions;
    uint32_t last_interaction_ms;
    uint8_t temperament; /* 0 balanced, 1 calm, 2 energetic, 3 shy, 4 dreamy */
    uint8_t initiative;  /* 0 medium, 1 high, 2 low */
    uint8_t quirk;       /* 0 none, 1 sleepy, 2 curious, 3 playful, 4 shy */
} companion_state_t;

void companion_emotion_init(companion_state_t *state);
void companion_emotion_tick(companion_state_t *state, uint32_t elapsed_seconds, bool is_sleeping);
void companion_emotion_interaction(companion_state_t *state, const char *kind);
companion_emotion_t companion_emotion_choose(const companion_state_t *state,
                                              uint8_t local_hour,
                                              uint32_t random_value,
                                              companion_emotion_t previous);
const char *companion_emotion_name(companion_emotion_t emotion);
