/* Motion events adapted from Pip-KeyPet ui/imu.h by David Novikov (MIT). */
#pragma once
#include <stdbool.h>
#include <stdint.h>

enum {
    COMPANION_MOTION_SHAKE = 1U,
    COMPANION_MOTION_FACE_DOWN = 2U,
    COMPANION_MOTION_FACE_UP = 4U,
    COMPANION_MOTION_STEP = 8U,
    COMPANION_MOTION_UPSIDE_DOWN = 16U,
    COMPANION_MOTION_UPRIGHT = 32U,
    COMPANION_MOTION_FALL = 64U
};

typedef struct {
    float tilt_x, tilt_y, energy;
    bool shaking, face_down, upside_down;
    uint32_t steps;
    bool initialized, above_step, falling;
    float base_x, base_y, shake_time, face_down_time, upside_time, fall_time;
    float magnitude_lp, magnitude_base, elapsed, last_step, upside_down_time;
} companion_motion_state_t;

uint32_t companion_motion_feed(companion_motion_state_t *state, float ax_g, float ay_g, float az_g, float elapsed_seconds);

typedef struct {
    float pitch, roll, yaw; /* radians; yaw is gyro-integrated and gently recentres */
    bool initialized;
} companion_orientation_t;

void companion_orientation_feed(companion_orientation_t *state,
    float ax_g, float ay_g, float az_g,
    float gx_rad_s, float gy_rad_s, float gz_rad_s,
    float elapsed_seconds);
