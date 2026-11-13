#include "voice.h"
#include <math.h>
#include <string.h>

#define MAX_SYL 14
#define TWO_PI 6.2831853f

/* One syllable: a pitched source through a "mouth" filter that opens and closes. */
typedef struct {
    float f0, f1;       /* pitch glide, Hz */
    float dur, gap;     /* s */
    float vib_rate, vib_depth;
    float rough;        /* growl: amplitude flutter */
    float bright;       /* mouth cutoff as a multiple of the pitch */
    float open;         /* how much the mouth opens mid-syllable (cutoff x) */
    float noise;        /* breath */
    float amp;
    uint8_t wave;       /* 0 soft (sine + harmonic), 1 buzzy (saw), 2 beep (square) */
} syl_t;

typedef struct { syl_t s[MAX_SYL]; int n; } phrase_t;

static struct {
    int rate;
    float volume;
    phrase_t cur;
    volatile int pending; /* set by voice_say from the UI side, picked up by the audio side */
    int i;                /* current syllable, == cur.n when idle */
    float t, phase, low, band;
    uint32_t rng, noise;
} v = {.rate = 16000, .volume = 0.8f, .pending = -1};

static uint32_t next(uint32_t *s) { *s ^= *s << 13, *s ^= *s >> 17, *s ^= *s << 5; return *s; }
static float rnd(float a, float b) { return a + (b - a) * (next(&v.rng) % 10000) / 10000.0f; }
static float semi(float n) { return powf(2, n / 12); }

static void add(phrase_t *p, syl_t s)
{
    if (p->n < MAX_SYL) {
        if (!s.amp) s.amp = 1;
        if (!s.bright) s.bright = 4;
        if (!s.open) s.open = 1.6f;
        p->s[p->n++] = s;
    }
}

/* every call makes a slightly different phrase, so he never sounds like a recording */
static void compose(phrase_t *p, int sound)
{
    memset(p, 0, sizeof *p);
    float base = 520 * semi(rnd(-2, 2));
    switch (sound) {
    case VOICE_POKE:
        add(p, (syl_t){.f0 = base * 1.4f, .f1 = base * 2.1f, .dur = .09f, .bright = 5});
        break;
    case VOICE_HAPPY:
        for (int i = 0, n = 2 + (int)rnd(0, 2); i < n; i++)
            add(p, (syl_t){.f0 = base * semi(i * 3), .f1 = base * semi(i * 3 + 5), .dur = rnd(.08f, .12f), .gap = .04f, .vib_rate = 9, .vib_depth = .02f});
        break;
    case VOICE_SAD:
        add(p, (syl_t){.f0 = base * .8f, .f1 = base * .55f, .dur = .45f, .gap = .08f, .vib_rate = 5, .vib_depth = .03f, .bright = 2.5f, .open = 1.2f});
        add(p, (syl_t){.f0 = base * .62f, .f1 = base * .42f, .dur = .5f, .vib_rate = 5, .vib_depth = .04f, .bright = 2.2f, .open = 1.1f, .noise = .05f});
        break;
    case VOICE_GROWL:
        for (int i = 0, n = 1 + (int)rnd(0, 2); i < n; i++)
            add(p, (syl_t){.f0 = base * .38f, .f1 = base * .3f, .dur = rnd(.2f, .3f), .gap = .05f, .rough = .9f, .bright = 6, .open = 1.4f, .wave = 1, .amp = .9f});
        break;
    case VOICE_OH:
        add(p, (syl_t){.f0 = base * .9f, .f1 = base * 1.9f, .dur = .16f, .gap = .03f, .open = 2.4f});
        add(p, (syl_t){.f0 = base * 1.7f, .f1 = base * 2.3f, .dur = .1f});
        break;
    case VOICE_LOVE:
        add(p, (syl_t){.f0 = base * 1.6f, .f1 = base * 1.1f, .dur = .7f, .vib_rate = 11, .vib_depth = .06f, .bright = 3.5f, .open = 1.8f});
        break;
    case VOICE_PURR:
        add(p, (syl_t){.f0 = 95, .f1 = 90, .dur = .9f, .rough = 1, .bright = 5, .open = 1.2f, .wave = 1, .noise = .1f, .amp = .55f});
        break;
    case VOICE_NOM:
        for (int i = 0; i < 3; i++)
            add(p, (syl_t){.f0 = base * .55f, .f1 = base * .5f, .dur = .08f, .gap = .07f, .bright = 1.8f, .open = 1.1f, .amp = .8f});
        add(p, (syl_t){.f0 = base, .f1 = base * 1.5f, .dur = .12f});
        break;
    case VOICE_BURP:
        add(p, (syl_t){.f0 = 130, .f1 = 85, .dur = .38f, .rough = 1, .bright = 7, .open = 1.5f, .wave = 1, .noise = .15f});
        break;
    case VOICE_YAWN:
        add(p, (syl_t){.f0 = base * .6f, .f1 = base * .95f, .dur = .45f, .bright = 2, .open = 2.5f, .noise = .12f, .amp = .7f});
        add(p, (syl_t){.f0 = base * .9f, .f1 = base * .45f, .dur = .6f, .bright = 2, .open = 1.4f, .noise = .1f, .amp = .6f});
        break;
    case VOICE_DIZZY:
        add(p, (syl_t){.f0 = base * 1.3f, .f1 = base * .6f, .dur = .8f, .vib_rate = 6, .vib_depth = .14f, .open = 1.4f});
        break;
    case VOICE_DERP:
        add(p, (syl_t){.f0 = base * .7f, .f1 = base * .5f, .dur = .25f, .vib_rate = 14, .vib_depth = .08f, .bright = 2.5f, .wave = 1});
        add(p, (syl_t){.f0 = base * .5f, .f1 = base * .75f, .dur = .2f, .vib_rate = 14, .vib_depth = .08f, .bright = 2.5f, .wave = 1});
        break;
    case VOICE_HM:
        add(p, (syl_t){.f0 = base * .7f, .f1 = base * 1.05f, .dur = .28f, .bright = 1.8f, .open = 1.6f});
        break;
    case VOICE_CENSOR: /* angry garble, then the bleep */
        for (int i = 0; i < 5; i++)
            add(p, (syl_t){.f0 = base * rnd(.4f, .8f), .f1 = base * rnd(.4f, .8f), .dur = rnd(.05f, .09f), .gap = .015f, .rough = .6f, .wave = 1, .bright = 6});
        add(p, (syl_t){.f0 = 1000, .f1 = 1000, .dur = .35f, .bright = 3, .open = 1, .wave = 2, .amp = .45f});
        break;
    case VOICE_ALARM: { /* a little rising tune, sung twice */
        static const float notes[] = {0, 4, 7, 12};
        for (int r = 0; r < 2; r++)
            for (int i = 0; i < 4; i++)
                add(p, (syl_t){.f0 = 660 * semi(notes[i]), .f1 = 660 * semi(notes[i] + .5f), .dur = .12f, .gap = i == 3 ? .25f : .03f, .vib_rate = 7, .vib_depth = .015f});
        break;
    }
    case VOICE_TICK: /* not Pip: a short dry click, like a picker wheel's detent */
        add(p, (syl_t){.f0 = 2600, .f1 = 1800, .dur = .014f, .bright = 2, .open = 1, .noise = .6f, .wave = 2, .amp = .5f});
        break;
    case VOICE_HMPH:
        add(p, (syl_t){.f0 = base * .5f, .f1 = base * .35f, .dur = .22f, .rough = .5f, .bright = 3, .open = 1.2f, .noise = .25f, .wave = 1});
        break;
    }
}

void voice_init(int rate) { v.rate = rate; v.i = v.cur.n = 0; v.rng = 0x1234567u; v.noise = 99; }
void voice_say(int sound) { if (sound >= 0 && sound < VOICE_COUNT) v.pending = sound; }
void voice_set_volume(float vol) { v.volume = vol < 0 ? 0 : vol > 1 ? 1 : vol; }
float voice_volume(void) { return v.volume; }
bool voice_busy(void) { return v.pending >= 0 || v.i < v.cur.n; }

const char *voice_name(int sound)
{
    static const char *const names[VOICE_COUNT] = {"poke", "happy", "sad", "growl", "oh", "love", "purr", "nom",
                                                   "burp", "yawn", "dizzy", "derp", "hm", "censor", "alarm", "hmph", "tick"};
    return sound >= 0 && sound < VOICE_COUNT ? names[sound] : "";
}

bool voice_render(int16_t *out, int n)
{
    int sound = v.pending;
    if (sound >= 0) { v.pending = -1; compose(&v.cur, sound); v.i = 0; v.t = 0; }
    bool loud = false;
    const float dt = 1.0f / v.rate;
    for (int k = 0; k < n; k++) {
        float y = 0;
        if (v.i < v.cur.n) {
            const syl_t *s = &v.cur.s[v.i];
            if (v.t < s->dur) {
                float x = v.t / s->dur;
                float f = s->f0 * powf(s->f1 / s->f0, x) * (1 + s->vib_depth * sinf(TWO_PI * s->vib_rate * v.t));
                v.phase += f * dt;
                v.phase -= floorf(v.phase);
                float src = s->wave == 1 ? 2 * v.phase - 1
                          : s->wave == 2 ? (v.phase < 0.5f ? 0.7f : -0.7f)
                                         : sinf(TWO_PI * v.phase) + 0.3f * sinf(2 * TWO_PI * v.phase);
                src += s->noise * ((int32_t)next(&v.noise) / 2147483648.0f);
                /* the mouth: a resonant low-pass whose cutoff opens mid-syllable (Chamberlin SVF) */
                float fc = fminf(f * s->bright * (1 + (s->open - 1) * sinf(3.14159f * x)), v.rate * 0.16f);
                float g = 2 * sinf(3.14159f * fc / v.rate);
                v.low += g * v.band;
                v.band += g * (src - v.low - 0.9f * v.band);
                float env = fminf(v.t / 0.008f, 1) * (x > 0.7f ? (1 - x) / 0.3f : 1);
                if (s->rough > 0) env *= 1 - s->rough * 0.45f * (1 + sinf(TWO_PI * 38 * v.t));
                y = v.low * env * s->amp;
            } else if (v.t >= s->dur + s->gap) {
                v.i++, v.t = 0;
            }
            v.t += dt;
        }
        y *= v.volume * 0.6f;
        y = y / (1 + fabsf(y)); /* soft clip */
        out[k] = (int16_t)(y * 32000);
        if (fabsf(y) > 0.01f) loud = true;
    }
    return loud;
}
