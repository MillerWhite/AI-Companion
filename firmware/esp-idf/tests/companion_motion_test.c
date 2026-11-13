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

    companion_orientation_t pose = {0};
    for (int i = 0; i < 80; ++i)
        companion_orientation_feed(&pose, 0.5f, 0.0f, 0.8660254f, 0, 0, 0, 0.01f);
    assert(pose.roll > 0.35f && pose.roll < 0.65f);
    pose = (companion_orientation_t){0};
    for (int i = 0; i < 80; ++i)
        companion_orientation_feed(&pose, 0.0f, -0.5f, 0.8660254f, 0, 0, 0, 0.01f);
    assert(pose.pitch > 0.35f && pose.pitch < 0.65f);
    pose = (companion_orientation_t){0};
    for (int i = 0; i < 100; ++i)
        companion_orientation_feed(&pose, 0, 0, 1, 0, 0, 0.7f, 0.01f);
    assert(pose.yaw > 0.5f && pose.yaw < 0.9f);
    for (int i = 0; i < 300; ++i)
        companion_orientation_feed(&pose, 0, 0, 1, 0, 0, 0, 0.01f);
    assert(pose.yaw < 0.4f);
    puts("companion motion tests passed");
    return 0;
}
