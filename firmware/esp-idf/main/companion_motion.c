/* Motion classifier adapted from Pip-KeyPet ui/imu.c by David Novikov (MIT).
 * Pure C; input acceleration is expressed in g.
 */
#include "companion_motion.h"
#include <math.h>

#define HAND_POSE_S 4.0f
#define TILT_GAIN 2.0f
#define SHAKE_G 0.5f
#define SHAKE_S 0.35f
#define FACE_DOWN_Z -0.8f
#define FACE_DOWN_S 1.0f
#define STEP_G 0.12f
#define STEP_MIN_S 0.28f
#define UPSIDE_Y -0.6f
#define UPSIDE_S 1.0f
#define FALL_G 0.35f
#define FALL_S 0.12f

static float lp(float value, float target, float dt, float tau)
{
    return value + (target - value) * (1.0f - expf(-dt / tau));
}

static float clamp1(float value)
{
    return value < -1.0f ? -1.0f : value > 1.0f ? 1.0f : value;
}

uint32_t companion_motion_feed(companion_motion_state_t *m, float ax, float ay, float az, float dt)
{
    if (!m || dt <= 0.0f) return 0;
    uint32_t events = 0;
    const float magnitude = sqrtf(ax * ax + ay * ay + az * az);
    if (!m->initialized) {
        m->base_x = ax;
        m->base_y = ay;
        m->magnitude_lp = magnitude;
        m->magnitude_base = magnitude;
        m->initialized = true;
    }
    m->elapsed += dt;

    m->base_x = lp(m->base_x, ax, dt, HAND_POSE_S);
    m->base_y = lp(m->base_y, ay, dt, HAND_POSE_S);
    m->tilt_x = clamp1(-(ax - m->base_x) * TILT_GAIN);
    m->tilt_y = clamp1((ay - m->base_y) * TILT_GAIN);

    m->energy = lp(m->energy, fabsf(magnitude - 1.0f), dt, 0.25f);
    if (m->energy > SHAKE_G) {
        m->shake_time += dt;
        if (m->shake_time > SHAKE_S && !m->shaking) {
            m->shaking = true;
            events |= COMPANION_MOTION_SHAKE;
        }
    } else if (m->energy < SHAKE_G * 0.4f) {
        m->shake_time = 0.0f;
        m->shaking = false;
    }

    if (az < FACE_DOWN_Z) {
        m->face_down_time += dt;
        if (!m->face_down && m->face_down_time > FACE_DOWN_S) {
            m->face_down = true;
            events |= COMPANION_MOTION_FACE_DOWN;
        }
    } else {
        m->face_down_time = 0.0f;
        if (m->face_down && az > FACE_DOWN_Z + 0.3f) {
            m->face_down = false;
            events |= COMPANION_MOTION_FACE_UP;
        }
    }

    if (ay < UPSIDE_Y && !m->face_down) {
        m->upside_time += dt;
        if (!m->upside && m->upside_time > UPSIDE_S) {
            m->upside = true;
            events |= COMPANION_MOTION_UPSIDE_DOWN;
        }
    } else {
        m->upside_time = 0.0f;
        if (m->upside && ay > UPSIDE_Y + 0.3f) {
            m->upside = false;
            events |= COMPANION_MOTION_UPRIGHT;
        }
    }

    if (magnitude < FALL_G && !m->shaking) {
        m->fall_time += dt;
        if (!m->falling && m->fall_time > FALL_S) {
            m->falling = true;
            events |= COMPANION_MOTION_FALL;
        }
    } else if (magnitude > 0.7f) {
        m->fall_time = 0.0f;
        m->falling = false;
    }

    m->magnitude_lp = lp(m->magnitude_lp, magnitude, dt, 0.05f);
    m->magnitude_base = lp(m->magnitude_base, magnitude, dt, 1.0f);
    const float bump = m->magnitude_lp - m->magnitude_base;
    if (!m->above_step && bump > STEP_G && m->elapsed - m->last_step > STEP_MIN_S && !m->shaking) {
        m->above_step = true;
        m->last_step = m->elapsed;
        ++m->steps;
        events |= COMPANION_MOTION_STEP;
    } else if (bump < STEP_G * 0.25f) {
        m->above_step = false;
    }
    return events;
}
