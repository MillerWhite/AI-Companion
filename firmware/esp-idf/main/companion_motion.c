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
        m->upside_down_time += dt;
        if (!m->upside_down && m->upside_down_time > UPSIDE_S) {
            m->upside_down = true;
            events |= COMPANION_MOTION_UPSIDE_DOWN;
        }
    } else {
        m->upside_down_time = 0.0f;
        if (m->upside_down && ay > UPSIDE_Y + 0.3f) {
            m->upside_down = false;
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

/* Complementary-filter orientation for expressive 3-axis eye movement.
 * Accelerometer stabilizes pitch/roll; gyro supplies fast response and relative yaw.
 * Yaw is intentionally a short-lived visual offset because an accelerometer cannot
 * observe absolute heading; it eases back instead of accumulating an unbounded drift.
 */
void companion_orientation_feed(companion_orientation_t *s,
    float ax, float ay, float az, float gx, float gy, float gz, float dt)
{
    if (!s || dt <= 0.0f || dt > 0.25f) return;
    const float norm = sqrtf(ax * ax + ay * ay + az * az);
    const bool gravity_valid = norm > 0.65f && norm < 1.35f;
    const float acc_pitch = atan2f(-ay, sqrtf(ax * ax + az * az));
    const float acc_roll = atan2f(ax, az);
    if (!s->initialized) {
        if (gravity_valid) {
            s->pitch = acc_pitch;
            s->roll = acc_roll;
        }
        s->initialized = true;
    }
    const float gyro_pitch = s->pitch + gy * dt;
    const float gyro_roll = s->roll + gx * dt;
    if (gravity_valid) {
        const float correction = 1.0f - expf(-dt * 3.5f);
        s->pitch = gyro_pitch + (acc_pitch - gyro_pitch) * correction;
        s->roll = gyro_roll + (acc_roll - gyro_roll) * correction;
    } else {
        s->pitch = gyro_pitch;
        s->roll = gyro_roll;
    }
    s->yaw += gz * dt;
    if (fabsf(gz) < 0.025f) s->yaw *= expf(-dt * 0.45f);
    if (s->pitch > 1.25f) s->pitch = 1.25f;
    if (s->pitch < -1.25f) s->pitch = -1.25f;
    if (s->roll > 1.25f) s->roll = 1.25f;
    if (s->roll < -1.25f) s->roll = -1.25f;
    if (s->yaw > 1.10f) s->yaw = 1.10f;
    if (s->yaw < -1.10f) s->yaw = -1.10f;
}
