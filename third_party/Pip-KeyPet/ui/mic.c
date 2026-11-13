#include "mic.h"
#include <math.h>

/* dB SPL = dBFS + this. ponytail: a guess from a typical MEMS mic (-26 dBFS at 94 dB SPL) and the gain the
   firmware sets; calibrate against a phone meter app held next to it and change the one number. */
#define MIC_SPL_OFFSET 96.0f
#define HP_HZ 120.0f        /* rumble and DC out: what a studio calls silence is not the building's hum */
#define LOW_HZ 300.0f       /* breath on a mic is mostly below this */
#define BLOW_DBFS -24.0f    /* that loud... */
#define BLOW_LOW 0.6f       /* ...with this much of it low... */
#define BLOW_S 0.25f        /* ...this long */

static struct {
    bool on, blowing;
    float dc, hp_lp, low;    /* filter states */
    double e, e_raw, e_low;  /* energy of the current 25 ms block: for the meter, and for breath (all of it, the low part) */
    int n, block;
    float power, db;         /* smoothed */
    float blow_t, calm_t;
} m = {.db = 0};

void mic_feed(const int16_t *s, int n, int rate)
{
    float a_dc = 1 - expf(-2 * 3.14159f * 20 / rate), a_hp = 1 - expf(-2 * 3.14159f * HP_HZ / rate), a_low = 1 - expf(-2 * 3.14159f * LOW_HZ / rate);
    m.block = rate / 40;
    m.on = true;
    for (int i = 0; i < n; i++) {
        float x = s[i] / 32768.0f;
        m.dc += (x - m.dc) * a_dc;
        float z = x - m.dc;          /* breath lives low: only the DC goes */
        m.hp_lp += (x - m.hp_lp) * a_hp;
        float y = x - m.hp_lp;       /* the meter leaves the rumble out */
        m.low += (z - m.low) * a_low;
        m.e += y * y, m.e_raw += z * z, m.e_low += m.low * m.low;
        if (++m.n < m.block) continue;
        /* a block: the level (RMS against full scale, so a full-scale sine is -3 dBFS), smoothed like a meter's "fast" */
        float p = (float)(m.e / m.n), low = m.e_raw > 0 ? (float)(m.e_low / m.e_raw) : 0, dt = (float)m.n / rate;
        m.power += (p - m.power) * (1 - expf(-dt / 0.125f));
        m.db = 10 * log10f(m.power + 1e-12f) + MIC_SPL_OFFSET;
        if (m.db < 0) m.db = 0;
        float block_dbfs = 10 * log10f((float)(m.e_raw / m.n) + 1e-12f);
        if (block_dbfs > BLOW_DBFS && low > BLOW_LOW) m.blow_t += dt, m.calm_t = 0;
        else if ((m.calm_t += dt) > 0.15f) m.blow_t = 0; /* a breath flutters: short gaps do not end it */
        m.blowing = m.blow_t > BLOW_S;
        m.e = m.e_raw = m.e_low = 0, m.n = 0;
    }
}

bool mic_on(void) { return m.on; }
float mic_db(void) { return m.db; }
bool mic_blowing(void) { return m.blowing; }
