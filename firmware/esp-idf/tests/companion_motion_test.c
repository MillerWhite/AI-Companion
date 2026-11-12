#include "companion_motion.h"
#include <assert.h>
#include <stdio.h>

int main(void)
{
    companion_motion_state_t state = {0};
    (void)companion_motion_feed(&state, 0.0f, 0.0f, 1.0f, 0.05f);
    uint32_t events = 0;
    for (int i = 0; i < 8; ++i)
        events |= companion_motion_feed(&state, 0.0f, 0.0f, -1.0f, 0.2f);
    assert((events & COMPANION_MOTION_FACE_DOWN) != 0);
    events = companion_motion_feed(&state, 0.0f, 0.0f, 1.0f, 0.05f);
    assert((events & COMPANION_MOTION_FACE_UP) != 0);

    state = (companion_motion_state_t){0};
    (void)companion_motion_feed(&state, 0.0f, 0.0f, 1.0f, 0.05f);
    events = 0;
    for (int i = 0; i < 20; ++i)
        events |= companion_motion_feed(&state, 0.0f, 0.0f, 2.2f, 0.05f);
    assert((events & COMPANION_MOTION_SHAKE) != 0);

    state = (companion_motion_state_t){0};
    (void)companion_motion_feed(&state, 0.0f, 0.0f, 1.0f, 0.05f);
    events = 0;
    for (int i = 0; i < 25; ++i)
        events |= companion_motion_feed(&state, 0.0f, -1.0f, 0.0f, 0.05f);
    assert((events & COMPANION_MOTION_UPSIDE_DOWN) != 0);
    puts("companion motion tests passed");
    return 0;
}
