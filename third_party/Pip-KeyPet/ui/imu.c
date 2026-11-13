#include "imu.h"
#include <math.h>

#define HAND_POSE_S 4.0f   /* how fast the "usual" holding angle adapts */
#define TILT_GAIN 2.0f
#define SHAKE_G 0.5f       /* smoothed linear acceleration that counts as shaking */
#define SHAKE_S 0.35f
#define FACE_DOWN_Z -0.8f
#define FACE_DOWN_S 1.0f
#define STEP_G 0.12f
#define STEP_MIN_S 0.28f
#define UPSIDE_Y -0.6f     /* the top of the screen points down */
#define UPSIDE_S 1.0f
#define FALL_G 0.35f       /* in free fall |a| drops toward 0 */
#define FALL_S 0.12f

static float lp(float v, float to, float dt, float tau) { return v + (to - v) * (1.0f - expf(-dt / tau)); }
static float clamp1(float v) { return v < -1 ? -1 : v > 1 ? 1 : v; }

int imu_feed(imu_t *m, float ax, float ay, float az, float dt)
{
    int ev = 0;
    float mag = sqrtf(ax * ax + ay * ay + az * az);
    if (!m->init) {
        m->bx = ax, m->by = ay, m->mag_lp = m->mag_base = mag;
        m->init = true;
    }
    m->t += dt;

    /* tilt: the accelerometer reads "up", so downhill is -a; compare with the slowly adapting hand pose */
    m->bx = lp(m->bx, ax, dt, HAND_POSE_S);
    m->by = lp(m->by, ay, dt, HAND_POSE_S);
    m->tilt_x = clamp1(-(ax - m->bx) * TILT_GAIN);
    m->tilt_y = clamp1((ay - m->by) * TILT_GAIN); /* device y is up, screen y is down */

    m->energy = lp(m->energy, fabsf(mag - 1.0f), dt, 0.25f);
    if (m->energy > SHAKE_G) {
        if ((m->shake_t += dt) > SHAKE_S && !m->shaking) {
            m->shaking = true;
            ev |= IMU_SHAKE;
        }
    } else if (m->energy < SHAKE_G * 0.4f) {
        m->shake_t = 0;
        m->shaking = false;
    }

    if (az < FACE_DOWN_Z) {
        if (!m->face_down && (m->down_t += dt) > FACE_DOWN_S) {
            m->face_down = true;
            ev |= IMU_FACE_DOWN;
        }
    } else {
        m->down_t = 0;
        if (m->face_down && az > FACE_DOWN_Z + 0.3f) {
            m->face_down = false;
            ev |= IMU_FACE_UP;
        }
    }

    if (ay < UPSIDE_Y && !m->face_down) {
        if (!m->upside && (m->flip_t += dt) > UPSIDE_S) m->upside = true, ev |= IMU_UPSIDE;
    } else {
        m->flip_t = 0;
        if (m->upside && ay > UPSIDE_Y + 0.3f) m->upside = false, ev |= IMU_UPRIGHT;
    }

    if (mag < FALL_G && !m->shaking) { /* dropped: once per fall (a shake tosses it about too) */
        if (!m->falling && (m->fall_t += dt) > FALL_S) m->falling = true, ev |= IMU_FALL;
    } else if (mag > 0.7f) {
        m->fall_t = 0, m->falling = false;
    }

    /* ponytail: a step is any bump in |a| above the running mean; real pedometers also demand a regular rhythm */
    m->mag_lp = lp(m->mag_lp, mag, dt, 0.05f);
    m->mag_base = lp(m->mag_base, mag, dt, 1.0f);
    float bump = m->mag_lp - m->mag_base;
    if (!m->above && bump > STEP_G && m->t - m->last_step > STEP_MIN_S && !m->shaking) /* a keychain bounces hard on a walk: only a real shake stops the count */ {
        m->above = true;
        m->last_step = m->t;
        m->steps++;
        ev |= IMU_STEP;
    } else if (bump < STEP_G * 0.25f) {
        m->above = false;
    }
    return ev;
}
