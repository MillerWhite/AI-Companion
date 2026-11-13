/* Portions adapted from Pip-KeyPet ui/face.c by David Novikov.
 * Copyright (c) 2026 David Novikov; MIT license, see third_party/Pip-KeyPet-LICENSE.txt.
 */
#include "companion_face_renderer.h"
#include <math.h>
#include <stdbool.h>
#include <string.h>

#define MAX_W 512
#ifdef FACE_PROFILE /* firmware bring-up: CPU cycles per pixel path (0 skipped, 1 white, 2 rim, 3 full) */
#include "esp_cpu.h"
long face_prof[8][2]; /* + 4 clear, 5 blush, 6 eye rows total */
#define PS uint32_t c0_ = esp_cpu_get_cycle_count();
#define PE(k) (face_prof[k][0] += esp_cpu_get_cycle_count() - c0_, face_prof[k][1]++)
#else
#define PS
#define PE(k) ((void)0)
#endif
#ifndef FACE_HOT
#define FACE_HOT /* the firmware puts the per-pixel code in internal RAM (IRAM) */
#endif

typedef struct { float x, y, z; } v3;
static inline v3 v3s(v3 a, float k) { return (v3){a.x * k, a.y * k, a.z * k}; }
static inline v3 v3sub(v3 a, v3 b) { return (v3){a.x - b.x, a.y - b.y, a.z - b.z}; }
static inline float dot(v3 a, v3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static inline float clamp01(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }
/* The ESP32-S3 has no FPU divide or square root: GCC calls slow library routines for them, and for fmaxf/fminf.
   The per-pixel code below uses these instead: plain compares, and 1/sqrt by bit trick plus two Newton steps
   (relative error ~5e-6, far below one RGB565 step). */
static inline float mx(float a, float b) { return a > b ? a : b; }
static inline float mn(float a, float b) { return a < b ? a : b; }
static inline float rsqrt(float x)
{
    union { float f; uint32_t i; } u = {x};
    u.i = 0x5F375A86u - (u.i >> 1);
    float y = u.f;
    y *= 1.5f - 0.5f * x * y * y;
    y *= 1.5f - 0.5f * x * y * y;
    return y;
}
static inline float sqrt_fast(float x) { return x > 0 ? x * rsqrt(x) : 0; }
static inline int mini(int a, int b) { return a < b ? a : b; }
static inline int maxi(int a, int b) { return a > b ? a : b; }
/* libm's sinf and atan2f cost hundreds of cycles there; for the tears and the veins these are plenty
   (errors ~2e-4 and ~1e-5 rad: a fraction of a pixel on the screen) */
static inline float sin_fast(float x)
{
    float t = x * 0.15915494f; /* turns */
    t -= (float)(int)t;
    if (t < 0) t += 1;
    float y = (t - 0.5f) * 6.2831853f; /* x - pi, in [-pi, pi): sin(x) = -sin(y) */
    if (y > 1.5707963f) y = 3.1415927f - y;
    else if (y < -1.5707963f) y = -3.1415927f - y;
    float y2 = y * y;
    return -y * (1 + y2 * (-0.16666667f + y2 * (0.0083333310f + y2 * -0.00019840874f)));
}
static inline float atan2_fast(float y, float x)
{
    float ax = fabsf(x), ay = fabsf(y), hi = mx(ax, ay);
    if (hi <= 0) return 0;
    float t = mn(ax, ay) / hi, t2 = t * t;
    float a = t * (0.99997726f + t2 * (-0.33262347f + t2 * (0.19354346f + t2 * (-0.11643287f + t2 * (0.05265332f + t2 * -0.01172120f)))));
    if (ay > ax) a = 1.5707963f - a;
    if (x < 0) a = 3.1415927f - a;
    return y < 0 ? -a : a;
}
static inline float off_int(float v) /* how far v is from the nearest whole number, 0..0.5 */
{
    float d = fabsf(v - (float)(int)v);
    return d > 0.5f ? 1 - d : d;
}
/* the canvas may keep its pixels byte-swapped, the panel's order, so LVGL copies it to the screen as it is */
static inline uint16_t swap_if(uint16_t c, bool sw) { return sw ? (uint16_t)(c << 8 | c >> 8) : c; }

static face_rect_t rect_union(face_rect_t a, face_rect_t b)
{
    if (a.x2 < a.x1) return b;
    if (b.x2 < b.x1) return a;
    return (face_rect_t){mini(a.x1, b.x1), mini(a.y1, b.y1), maxi(a.x2, b.x2), maxi(a.y2, b.y2)};
}

/* rgb = eye * shade blended toward pupil by p, laid over dst with coverage a.
   Ordered dithering hides RGB565 banding in the soft edges. */
static uint16_t pixel(uint16_t eye, uint16_t pupil, float shade, float p, float a, uint16_t dst, int x, int y)
{
    static const float bayer[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
    float d = (bayer[y & 3][x & 3] + 0.5f) / 16.0f;
    float e = shade * (1 - p) * a, q = p * a, k = 1 - a;
    int r = (int)(((eye >> 11) & 31) * e + ((pupil >> 11) & 31) * q + ((dst >> 11) & 31) * k + d);
    int g = (int)(((eye >> 5) & 63) * e + ((pupil >> 5) & 63) * q + ((dst >> 5) & 63) * k + d);
    int b = (int)((eye & 31) * e + (pupil & 31) * q + (dst & 31) * k + d);
    return (uint16_t)((r > 31 ? 31 : r) << 11 | (g > 63 ? 63 : g) << 5 | (b > 31 ? 31 : b));
}

/* Rounded box SDF (negative inside) */
static float rbox(float x, float y, float hw, float hh, float r)
{
    float qx = fabsf(x) - hw + r, qy = fabsf(y) - hh + r;
    float ox = mx(qx, 0), oy = mx(qy, 0);
    return sqrt_fast(ox * ox + oy * oy) + mn(mx(qx, qy), 0) - r;
}

/* Heart SDF (Inigo Quilez), y up, tip at the origin, about 1 tall */
static inline float heart(float x, float y)
{
    x = fabsf(x);
    if (y + x > 1) {
        float dx = x - 0.25f, dy = y - 0.75f;
        return sqrt_fast(dx * dx + dy * dy) - 0.35355f;
    }
    float ax = x, ay = y - 1, m = 0.5f * mx(x + y, 0), bx = x - m, by = y - m;
    float d = mn(ax * ax + ay * ay, bx * bx + by * by);
    return sqrt_fast(d) * (x > y ? 1 : -1);
}

static uint16_t mix565(uint16_t a, uint16_t b, float t)
{
    int r = (int)(((a >> 11) & 31) * (1 - t) + ((b >> 11) & 31) * t + 0.5f);
    int g = (int)(((a >> 5) & 63) * (1 - t) + ((b >> 5) & 63) * t + 0.5f);
    int bl = (int)((a & 31) * (1 - t) + (b & 31) * t + 0.5f);
    return (uint16_t)(r << 11 | g << 5 | bl);
}

/* everything about an eye that is the same for every pixel of a frame */
typedef struct {
    float cr, sr, A, B, C, ext_x, ext_y, a, b;
    v3 c0, c1, d;
    float dd, shx, shy, soft, w2c, w2s, w3c, w3s, w5c, w5s;
    v3 g, t1, t2;
    float pw, ph, prr, hm, hs, inv_hs;
    uint16_t pupil_color;
    float lid, smile, lid_y, lid_n, cut_r, cut_y, taper, grad_hint, inv_ext_y, inv_dd;
    float in_k, lit1_k, lit0_k; /* squared distance thresholds for the fast paths */
    face_rect_t pupil;          /* screen box the pupil (and its glints, heart, spiral) can touch */
    float r_min, inv_A, wb;     /* the silhouette's minor semi-axis (model px), for the per-row spans */
    face_rect_t r;
} eye_setup_t;

static void eye_prepare(const face_t *f, const eye_t *e, int w, int h, eye_setup_t *s)
{
    /* world = Rz(roll) Ry(yaw) Rx(pitch) local; local unit sphere = S^-1 R^T world */
    float cy_ = cosf(e->yaw), sy_ = sinf(e->yaw), cp = cosf(e->pitch), sp = sinf(e->pitch);
    float cr = cosf(e->roll), sr = sinf(e->roll);
    /* R columns are the local axes in world space */
    v3 ry_x = {cy_, 0, -sy_}, ry_y = {-sy_ * sp, cp, -cy_ * sp}, ry_z = {sy_ * cp, sp, cy_ * cp};
    v3 ax = {cr * ry_x.x - sr * ry_x.y, sr * ry_x.x + cr * ry_x.y, ry_x.z};
    v3 ay = {cr * ry_y.x - sr * ry_y.y, sr * ry_y.x + cr * ry_y.y, ry_y.z};
    v3 az = {cr * ry_z.x - sr * ry_z.y, sr * ry_z.x + cr * ry_z.y, ry_z.z};
    float a = fmaxf(e->w * 0.5f, 1), b = fmaxf(e->h * 0.5f, 1), c = fmaxf(e->depth * 0.5f, 1);
    /* screen axis i maps to local (ax.i/a, ay.i/b, az.i/c) */
    v3 c0 = {ax.x / a, ay.x / b, az.x / c}, c1 = {ax.y / a, ay.y / b, az.y / c}, d = {ax.z / a, ay.z / b, az.z / c};
    float dd = dot(d, d);
    v3 p0 = v3sub(c0, v3s(d, dot(c0, d) / dd)), p1 = v3sub(c1, v3s(d, dot(c1, d) / dd));
    float A = dot(p0, p0), B = dot(p0, p1), C = dot(p1, p1), det = A * C - B * B; /* silhouette: A x^2 + 2Bxy + C y^2 = 1 */
    float ext_x = sqrtf(C / det), ext_y = sqrtf(A / det);
    s->cr = cr, s->sr = sr, s->A = A, s->B = B, s->C = C, s->ext_x = ext_x, s->ext_y = ext_y, s->a = a, s->b = b;
    s->c0 = c0, s->c1 = c1, s->d = d, s->dd = dd;

    /* cartoon shadow: what the silhouette shifted toward the light (upper left) leaves uncovered */
    float shx = 0.06f * ext_x, shy = 0.075f * ext_y, soft = 1.0f / (0.09f * fminf(ext_x, ext_y) + 1); /* blur ~9% of the eye */
    /* living outline: three slow harmonics around the rim */
    float wt = e->wob_t, w2c = cosf(1.3f * wt), w2s = sinf(1.3f * wt), w3c = cosf(1 - 1.7f * wt), w3s = sinf(1 - 1.7f * wt);
    float w5c = cosf(2 + 2.3f * wt), w5s = sinf(2 + 2.3f * wt);
    s->shx = shx, s->shy = shy, s->soft = soft, s->w2c = w2c, s->w2s = w2s, s->w3c = w3c, s->w3s = w3s, s->w5c = w5c, s->w5s = w5s;

    /* pupil frame on the unit ball */
    float gyw = e->look_x, gpt = e->look_y;
    v3 g = {sinf(gyw) * cosf(gpt), sinf(gpt), cosf(gyw) * cosf(gpt)};
    v3 t1 = {cosf(gyw), 0, -sinf(gyw)};
    v3 t2 = {g.y * t1.z - g.z * t1.y, g.z * t1.x - g.x * t1.z, g.x * t1.y - g.y * t1.x};
    float pw = fmaxf(e->pw, 0.01f), ph = fmaxf(e->ph, 0.01f), prr = (1 - clamp01(e->square)) * fminf(pw, ph);
    float hm = clamp01(e->heart), hs = fmaxf(pw, ph) * 2.1f;
    uint16_t pupil_color = mix565(f->pupil_color, f->heart_color, hm);
    s->g = g, s->t1 = t1, s->t2 = t2, s->pw = pw, s->ph = ph, s->prr = prr, s->hm = hm, s->hs = hs, s->inv_hs = 1.0f / hs;
    s->pupil_color = pupil_color;

    /* 2D cuts in the eye's rolled frame, px */
    /* dead zone: a spring settling around 0 must not shave a few pixels off the eye */
    float lid = (e->lid - 0.05f) / 0.95f, smile = (e->smile - 0.05f) / 0.95f;
    float lid_y = -ext_y + lid * 2 * ext_y, lid_n = 1.0f / sqrtf(1 + e->tilt * e->tilt);
    float cut_r = ext_x * 1.6f, cut_y = ext_y + 1.5f - smile * 1.7f * ext_y + cut_r;
    float taper = fmaxf(-0.6f, fminf(0.6f, e->taper));
    s->lid = lid, s->smile = smile, s->lid_y = lid_y, s->lid_n = lid_n, s->cut_r = cut_r, s->cut_y = cut_y, s->taper = taper;
    /* fast paths, as squared distances to the rim (and to the shadow's rim) that settle a pixel without the slow math:
       fully covered whatever the wobble does; fully lit; fully in the shadow crescent */
    float wb = fabsf(e->wobble);
    s->in_k = (0.5f + wb) * (0.5f + wb) * 4;
    s->lit1_k = (0.6f / soft + wb) * (0.6f / soft + wb) * 4;
    s->lit0_k = (0.4f / soft + wb) * (0.4f / soft + wb) * 4;

    float pad = 2 + fabsf(e->wobble); /* the living outline reaches past the ellipse */
    float bx = ext_x * (1 + fabsf(taper)) + pad, by = ext_y + pad;
    face_rect_t r = {maxi(0, (int)floorf(e->x - bx)), maxi(0, (int)floorf(e->y - by)),
                     mini(w - 1, (int)ceilf(e->x + bx)), mini(h - 1, (int)ceilf(e->y + by))};
    if (r.x2 - r.x1 >= MAX_W) r.x2 = r.x1 + MAX_W - 1;
    s->r = r;
    s->grad_hint = 1.0f / (fminf(a, b) * 0.5f);
    /* the pupil's screen box: its outline (with room for the heart) taken onto the ball, through the eye's rotation and
       scale to the screen, plus the egg taper; a few px of margin keep the antialiasing gradient honest */
    {
        float ex = fmaxf(pw, hm > 0 ? hs * 0.6f : 0) + 0.08f, ey = fmaxf(ph, hm > 0 ? hs * 0.6f : 0) + 0.08f;
        float minx = 1e9f, miny = 1e9f, maxx = -1e9f, maxy = -1e9f;
        for (int k = 0; k < 24; k++) {
            float t = k * 6.2831853f / 24, ps = cosf(t) * ex, pt = sinf(t) * ey;
            float sq = 1 - ps * ps - pt * pt, gz = sq > 0 ? sqrtf(sq) : 0;
            v3 q = {g.x * gz + t1.x * ps + t2.x * pt, g.y * gz + t1.y * ps + t2.y * pt, g.z * gz + t1.z * ps + t2.z * pt};
            float wx = ax.x * a * q.x + ay.x * b * q.y + az.x * c * q.z, wy = ax.y * a * q.x + ay.y * b * q.y + az.y * c * q.z;
            wx *= 1 + taper * wy / ext_y; /* undo the per-row squeeze */
            minx = fminf(minx, wx), maxx = fmaxf(maxx, wx), miny = fminf(miny, wy), maxy = fmaxf(maxy, wy);
        }
        s->pupil = (face_rect_t){(int)floorf(e->x + minx) - 4, (int)floorf(e->y + miny) - 4, (int)ceilf(e->x + maxx) + 4, (int)ceilf(e->y + maxy) + 4};
    }
    s->inv_ext_y = 1.0f / ext_y, s->inv_dd = 1.0f / dd;
    s->r_min = 1.0f / sqrtf((A + C) * 0.5f + sqrtf((A - C) * (A - C) * 0.25f + B * B)), s->inv_A = 1.0f / A, s->wb = wb;
}

/* where along a row (model x) the ellipse A x^2 + 2B x y + C y^2 <= q lies; false if nowhere */
static inline bool span(const eye_setup_t *s, float y, float q, float *x0, float *x1)
{
    float by = s->B * y, disc = by * by - s->A * (s->C * y * y - q);
    if (q <= 0 || disc <= 0) return false;
    float r = sqrt_fast(disc);
    *x0 = (-by - r) * s->inv_A, *x1 = (-by + r) * s->inv_A;
    return true;
}

/* the pupil's signed distance (ball units) and the nearest catchlight's, at a screen point; false where the ball
   faces away from the pupil (pd is then a stand-in) */
static inline bool pupil_at(const eye_t *e, const eye_setup_t *s, float X, float Y, float *pd, float *gd)
{
    float sy = Y - e->y, sx = (X - e->x) / (1 + s->taper * sy * s->inv_ext_y);
    float k = 1 - (s->A * sx * sx + 2 * s->B * sx * sy + s->C * sy * sy);
    v3 o = {sx * s->c0.x + sy * s->c1.x, sx * s->c0.y + sy * s->c1.y, sx * s->c0.z + sy * s->c1.z};
    float t = (-dot(o, s->d) + sqrt_fast(s->dd * k)) * s->inv_dd;
    v3 p = {o.x + t * s->d.x, o.y + t * s->d.y, o.z + t * s->d.z};
    *pd = 1, *gd = 1;
    if (dot(p, s->g) <= 0) return false;
    float ps = dot(p, s->t1), pt = dot(p, s->t2);
    *pd = rbox(ps, pt, s->pw, s->ph, s->prr);
    if (s->hm > 0) *pd += (heart(ps * s->inv_hs, -pt * s->inv_hs + 0.5f) * s->hs - *pd) * s->hm; /* as eye_rows has it */
    if (e->glint > 0) {
        float m = mn(s->pw, s->ph) * clamp01(e->glint), ax1 = ps + 0.35f * s->pw, ay1 = pt + 0.38f * s->ph, ax2 = ps - 0.30f * s->pw, ay2 = pt - 0.32f * s->ph;
        *gd = mn(sqrt_fast(ax1 * ax1 + ay1 * ay1) - 0.32f * m, sqrt_fast(ax2 * ax2 + ay2 * ay2) - 0.14f * m);
    }
    return true;
}

#define PB_MAX 192 /* pupil box width in 2-px blocks */
typedef struct { int y; float pd[PB_MAX + 1], gd[PB_MAX + 1]; uint8_t real[PB_MAX + 1]; } pupil_line_t;
/* scratch for one band of rows; two bands render at once on the two cores */
typedef struct { float prev_row[MAX_W]; pupil_line_t lines[2]; } band_t;

/* rows y0..y1-1 of one eye; two cores can each take a band */
FACE_HOT static void eye_rows(const face_t *f, const eye_t *e, const eye_setup_t *s, uint16_t *buf, int w, int y0, int y1, band_t *band)
{
    const float cr = s->cr, sr = s->sr, A = s->A, B = s->B, C = s->C, ext_y = s->ext_y, a = s->a, b = s->b;
    const v3 c0 = s->c0, c1 = s->c1, d = s->d, g = s->g, t1 = s->t1, t2 = s->t2;
    const float dd = s->dd, shx = s->shx, shy = s->shy, soft = s->soft;
    const float w2c = s->w2c, w2s = s->w2s, w3c = s->w3c, w3s = s->w3s, w5c = s->w5c, w5s = s->w5s;
    const float pw = s->pw, ph = s->ph, prr = s->prr, hm = s->hm, hs = s->hs, inv_hs = 1.0f / hs;
    const float lid = s->lid, smile = s->smile, lid_y = s->lid_y, lid_n = s->lid_n, cut_r = s->cut_r, cut_y = s->cut_y, taper = s->taper;
    const float grad_hint = s->grad_hint;
    const uint16_t pupil_color = s->pupil_color;
    const bool sw = f->swapped, plain_eye = e->veins <= 0 && e->wet <= 0;
    const uint16_t ink_white = swap_if(f->color, sw), ink_pupil = swap_if(pupil_color, sw); /* as they go into buf */
    /* the tears reach this far up the eye frame (ev, see below); rows wholly above them are as plain as a dry eye */
    const float dry_v = ext_y * (1 - 0.34f * clamp01(e->wet)) - 1.5f - 3.0f;
    const float u_max = mx(fabsf(s->r.x1 - e->x), fabsf(s->r.x2 + 1 - e->x));
    face_rect_t r = s->r;
    if (y0 < r.y1) y0 = r.y1;
    if (y1 > r.y2 + 1) y1 = r.y2 + 1;
    if (y0 >= y1) return;

    /* pupil SDF from the previous row/pixel, for screen-space antialiasing */
    float *prev_row = band->prev_row;
    for (int i = 0; i <= r.x2 - r.x1; i++) prev_row[i] = NAN;
    /* Inside the pupil's box the field is sampled on a 2 px grid (pixel corners, a line per 2 rows, cached):
       a 2x2 block whose corners are all well inside, or all well outside, the pupil is filled flat. Only its rim and
       the catchlights get the full per-pixel treatment. Not for the spiral. */
    pupil_line_t *lines = band->lines;
    const face_rect_t P = s->pupil;
    const int pbw = (P.x2 - P.x1 + 2) / 2;
    const bool blocks = e->spiral <= 0 && pbw <= PB_MAX;
    lines[0].y = lines[1].y = -100000;

    const float inv_dd = s->inv_dd;
    for (int y = y0; y < y1; y++) {
        float sy = y + 0.5f - e->y, left = NAN;
        const bool plain = plain_eye || (e->veins <= 0 && cr * sy + fabsf(sr) * u_max < dry_v);
        float squeeze = 1.0f / (1 + taper * sy * s->inv_ext_y); /* the egg taper, once a row */
        /* Per-row spans, solved from the ellipse instead of tested per pixel (floats are slow on the ESP32):
           [xa, xb] may touch the eye at all; [fa, fb] minus up to two holes is plain lit white, filled flat.
           Margins are in screen px, scaled to model units on the safe side. */
        float mf = squeeze > 1 ? squeeze : 1, inv_sq = 1.0f / squeeze, xo = e->x - 0.5f;
        float m_out = (1.5f + s->wb) * mf, s0, s1;
        int xa = r.x2 + 1, xb = r.x1 - 1, fa = 1, fb = 0, h0a = 1, h0b = 0, h1a = 1, h1b = 0;
        int k0a = 1, k0b = 0, k1a = 1, k1b = 0; /* cut away by the lid / inside the smile's circle: nothing to draw */
        if (lid > 0) {
            float al = lid_n * (e->tilt * cr + sr), be = lid_n * (lid_y + e->tilt * sr * sy - cr * sy) + 0.5f - 2.0f;
            if (fabsf(al) < 1e-6f) { if (be >= 0) k0a = r.x1, k0b = r.x2; }
            else if (al > 0) k0a = (int)ceilf(-be / al + e->x - 0.5f), k0b = r.x2;
            else k0a = r.x1, k0b = (int)floorf(-be / al + e->x - 0.5f);
        }
        if (smile > 0) {
            float ucx = -sr * cut_y, vcy = cr * cut_y, R = cut_r - 2.0f, dv = sy - vcy;
            if (R > 0 && dv * dv < R * R) {
                float h = sqrt_fast(R * R - dv * dv);
                k1a = (int)ceilf(e->x - 0.5f + ucx - h), k1b = (int)floorf(e->x - 0.5f + ucx + h);
            }
        }
        float q_out = (1 + m_out / s->r_min) * (1 + m_out / s->r_min);
        if (span(s, sy, q_out, &s0, &s1)) xa = (int)floorf(xo + s0 * inv_sq) - 1, xb = (int)ceilf(xo + s1 * inv_sq) + 1;
        float m_in = (0.5f + s->wb + 0.5f) * mf, m_lit = (0.6f / soft + s->wb + 0.5f) * mf, i0, i1, l0, l1;
        const bool shadow = f->shading > 0;
        if (!shadow) m_lit = 0;
        if (plain && m_in < s->r_min && m_lit < s->r_min && span(s, sy, (1 - m_in / s->r_min) * (1 - m_in / s->r_min), &i0, &i1) &&
            (!shadow || span(s, sy + shy, (1 - m_lit / s->r_min) * (1 - m_lit / s->r_min), &l0, &l1))) {
            if (!shadow) l0 = i0 + shx, l1 = i1 + shx; /* no crescent: lit wherever covered */
            float lo = mx(i0, l0 - shx), hi = mn(i1, l1 - shx);
            fa = (int)ceilf(xo + lo * inv_sq), fb = (int)floorf(xo + hi * inv_sq);
            if (lid > 0) { /* the lid's half-plane, linear in u along the row */
                float al = lid_n * (e->tilt * cr + sr), be = lid_n * (lid_y + e->tilt * sr * sy - cr * sy) + 0.5f + 0.25f;
                if (fabsf(al) < 1e-6f) { if (be > 0) fb = fa - 1; }
                else if (al > 0) fb = (int)mn((float)fb, floorf(-be / al + xo));
                else fa = (int)mx((float)fa, ceilf(-be / al + xo));
            }
            if (smile > 0) { /* the smile's circle: a hole where it cuts the row */
                float ucx = -sr * cut_y, vcy = cr * cut_y, R = cut_r + 1.5f, dv = sy - vcy;
                if (dv * dv < R * R) {
                    float h = sqrt_fast(R * R - dv * dv);
                    h0a = (int)floorf(xo + ucx - h) - 1, h0b = (int)ceilf(xo + ucx + h) + 1;
                }
            }
            if (y >= s->pupil.y1 && y <= s->pupil.y2) h1a = s->pupil.x1, h1b = s->pupil.x2;
        }
        pupil_line_t *L0 = NULL, *L1 = NULL;
        float bfy = 0;
        if (blocks && y >= P.y1 && y <= P.y2) { /* the two sample lines around this row */
            int ly = P.y1 + ((y - P.y1) & ~1);
            for (int n = 0; n < 2; n++) {
                int want = ly + 2 * n, other = ly + 2 * (1 - n);
                pupil_line_t *l = lines[0].y == want ? &lines[0] : lines[1].y == want ? &lines[1] : NULL;
                if (!l) { /* refill the slot that does not hold the other line this row needs */
                    l = lines[0].y == other ? &lines[1] : &lines[0];
                    l->y = want;
                    for (int b = 0; b <= pbw; b++) l->real[b] = pupil_at(e, s, (float)(P.x1 + 2 * b), (float)want, &l->pd[b], &l->gd[b]);
                }
                if (n == 0) L0 = l; else L1 = l;
            }
            bfy = (y + 0.5f - ly) * 0.5f;
        }
        for (int x = r.x1, i = 0; x <= r.x2; x++, i++) {
            if (x < xa || x > xb || (x >= k0a && x <= k0b) || (x >= k1a && x <= k1b)) { prev_row[i] = left = NAN; continue; } /* nothing here */
            if (x >= fa && x <= fb && !(x >= h0a && x <= h0b) && !(x >= h1a && x <= h1b)) {
                buf[y * w + x] = ink_white; /* the flat lit white */
                prev_row[i] = left = NAN;
                continue;
            }
            if (L0 && x >= P.x1 && x <= P.x2 && x >= fa && x <= fb && !(x >= h0a && x <= h0b)) {
                int bx = (x - P.x1) >> 1;
                float c00 = L0->pd[bx], c01 = L0->pd[bx + 1], c10 = L1->pd[bx], c11 = L1->pd[bx + 1];
                float lo = mn(mn(c00, c01), mn(c10, c11)), hi = mx(mx(c00, c01), mx(c10, c11)), thr = hi - lo + grad_hint;
                bool all_real = L0->real[bx] && L0->real[bx + 1] && L1->real[bx] && L1->real[bx + 1];
                float gmin = mn(mn(L0->gd[bx], L0->gd[bx + 1]), mn(L1->gd[bx], L1->gd[bx + 1]));
                bool white = lo > thr, black = hi < -thr && (e->glint <= 0 || gmin > 2 * thr);
                if (white || black) {
                    buf[y * w + x] = black ? ink_pupil : ink_white;
                    float fx = (x + 0.5f - (P.x1 + 2 * bx)) * 0.5f; /* the field, bilinear: neighbours' gradients stay true */
                    float pdi = (c00 + (c01 - c00) * fx) * (1 - bfy) + (c10 + (c11 - c10) * fx) * bfy;
                    prev_row[i] = left = all_real ? pdi : NAN;
                    continue;
                }
            }
            PS
            float sx = x + 0.5f - e->x;
            float u = sx, v = sy;
            sx *= squeeze;
            float Q = A * sx * sx + 2 * B * sx * sy + C * sy * sy;
            float gx = A * sx + B * sy, gy = B * sx + C * sy, g2 = gx * gx + gy * gy, k = 1 - Q;
            if (k < 0 && k * k >= s->in_k * g2) { PE(0); prev_row[i] = left = NAN; continue; } /* surely outside */
            bool inside = k > 0 && k * k >= s->in_k * g2;                                /* surely covered */
            float wob = 0;
            if (!inside && e->wobble > 0) {
                float rl = rsqrt(sx * sx + sy * sy + 1e-6f), c1_ = sx * rl, s1_ = sy * rl;
                float c2 = c1_ * c1_ - s1_ * s1_, s2 = 2 * s1_ * c1_;
                float c3 = c2 * c1_ - s2 * s1_, s3 = s2 * c1_ + c2 * s1_;
                float c5 = c3 * c2 - s3 * s2, s5 = s3 * c2 + c3 * s2;
                wob = e->wobble * (0.3f * (s2 * w2c + c2 * w2s) + 0.5f * (s3 * w3c + c3 * w3s) + 0.2f * (s5 * w5c + c5 * w5s));
            }
            float cov = inside ? 1 : clamp01(0.5f + wob + k * 0.5f * rsqrt(g2 + 1e-12f));
            if (cov <= 0) { PE(2); prev_row[i] = left = NAN; continue; } /* leave what is behind */

            /* lids and smile are flat cuts in the rolled eye frame */
            float eu = cr * u + sr * v, ev = -sr * u + cr * v;
            if (lid > 0) cov *= 1 - clamp01((lid_y + e->tilt * eu - ev) * lid_n + 0.5f);
            if (smile > 0) {
                float dy = ev - cut_y;
                cov *= 1 - clamp01(cut_r - sqrt_fast(eu * eu + dy * dy) + 0.5f);
            }

            bool near_pupil = x >= s->pupil.x1 && x <= s->pupil.x2 && y >= s->pupil.y1 && y <= s->pupil.y2;
            if (!near_pupil && plain && f->shading <= 0) { /* the rim, away from the pupil: white over what is behind */
                buf[y * w + x] = cov >= 1 ? ink_white : swap_if(pixel(f->color, 0, 1, 0, cov, swap_if(buf[y * w + x], sw), x, y), sw);
                prev_row[i] = left = NAN;
                PE(1);
                continue;
            }
            if (!near_pupil && plain && cov >= 1) { /* most of the white: lit, uncut, away from the pupil */
                float qx = sx + shx, qy = sy + shy, Qo = A * qx * qx + 2 * B * qx * qy + C * qy * qy;
                float ox = A * qx + B * qy, oy = B * qx + C * qy, ko = 1 - Qo;
                if (f->shading <= 0 || (ko > 0 && ko * ko >= s->lit1_k * (ox * ox + oy * oy))) {
                    buf[y * w + x] = ink_white; PE(1);
                    prev_row[i] = left = NAN;
                    continue;
                }
            }
            /* front hit on the unit ball (rim-projected when just outside): only the pupil and the veins need it */
            v3 p = {0, 0, 0};
            if (near_pupil || !plain) {
                v3 o = {sx * c0.x + sy * c1.x, sx * c0.y + sy * c1.y, sx * c0.z + sy * c1.z};
                float od = dot(o, d), t = (-od + sqrt_fast(dd * k)) * inv_dd;
                p = (v3){o.x + t * d.x, o.y + t * d.y, o.z + t * d.z};
            }

            float qx = sx + shx, qy = sy + shy, Qo = A * qx * qx + 2 * B * qx * qy + C * qy * qy;
            float ox = A * qx + B * qy, oy = B * qx + C * qy, o2 = ox * ox + oy * oy, ko = 1 - Qo, lit;
            if (f->shading <= 0) lit = 1;                         /* no shadow at all */
            else if (ko > 0 && ko * ko >= s->lit1_k * o2) lit = 1; /* well inside the lit part */
            else if (ko < 0 && ko * ko >= s->lit0_k * o2) lit = 0; /* deep in the crescent */
            else {
                if (inside && e->wobble > 0) { /* the wobble was skipped above; the shadow edge needs it */
                    float rl = rsqrt(sx * sx + sy * sy + 1e-6f), c1_ = sx * rl, s1_ = sy * rl;
                    float c2 = c1_ * c1_ - s1_ * s1_, s2 = 2 * s1_ * c1_;
                    float c3 = c2 * c1_ - s2 * s1_, s3 = s2 * c1_ + c2 * s1_;
                    float c5 = c3 * c2 - s3 * s2, s5 = s3 * c2 + c3 * s2;
                    wob = e->wobble * (0.3f * (s2 * w2c + c2 * w2s) + 0.5f * (s3 * w3c + c3 * w3s) + 0.2f * (s5 * w5c + c5 * w5s));
                }
                lit = clamp01(0.4f + (wob + ko * 0.5f * rsqrt(o2 + 1e-12f)) * soft);
            }
            lit = lit * lit * (3 - 2 * lit); /* smoothstep: soft, but over a short distance */
            float shade = 1 - f->shading * 0.24f * (1 - lit);

            float pd = 1.0f, ps = 0, pt = 0;
            bool real = near_pupil && dot(p, g) > 0; /* elsewhere pd is a stand-in: no gradient may be taken from it */
            if (real) {
                ps = dot(p, t1), pt = dot(p, t2);
                pd = rbox(ps, pt, pw, ph, prr);
                if (hm > 0) pd += (heart(ps * inv_hs, -pt * inv_hs + 0.5f) * hs - pd) * hm;
            }
            float gl = isnan(left) ? NAN : pd - left, gu = isnan(prev_row[i]) ? NAN : pd - prev_row[i];
            /* far from the pupil's edge the answer is 0 or 1 without the exact gradient (it is at most 1.42 x the larger step) */
            float m = isnan(gl) ? (isnan(gu) ? grad_hint : fabsf(gu)) : isnan(gu) ? fabsf(gl) : mx(fabsf(gl), fabsf(gu));
            float grad = -1, pc;
            if (pd > 0.75f * m) pc = 0;
            else if (pd < -0.75f * m) pc = 1;
            else {
                grad = isnan(gl) ? (isnan(gu) ? grad_hint : fabsf(gu)) : isnan(gu) ? fabsf(gl) : sqrt_fast(gl * gl + gu * gu);
                float gg = mx(grad, 1e-4f);
                pc = clamp01(0.5f - pd * rsqrt(gg * gg));
            }
            if (pc > 0 && grad < 0 && (e->glint > 0 || e->spiral > 0))
                grad = isnan(gl) ? (isnan(gu) ? grad_hint : fabsf(gu)) : isnan(gu) ? fabsf(gl) : sqrt_fast(gl * gl + gu * gu);
            prev_row[i] = left = real ? pd : NAN;
            if (pc > 0 && e->spiral > 0) { /* two arms winding in, turning with the eye's clock */
                float ang = atan2_fast(pt, ps), rr_ = sqrt_fast(ps * ps + pt * pt) / mx(pw, ph);
                float st = sin_fast(2 * ang + rr_ * 14 - e->wob_t * 6);
                pc *= 1 - clamp01(e->spiral) * clamp01(0.5f + st * 2.5f) * clamp01(rr_ * 3);
            }
            if (pc > 0 && e->glint > 0) { /* catchlights stay up-left, toward the light */
                float m = mn(pw, ph) * clamp01(e->glint), ax1 = ps + 0.35f * pw, ay1 = pt + 0.38f * ph, ax2 = ps - 0.30f * pw, ay2 = pt - 0.32f * ph;
                float d1 = sqrt_fast(ax1 * ax1 + ay1 * ay1) - 0.32f * m;
                float d2 = sqrt_fast(ax2 * ax2 + ay2 * ay2) - 0.14f * m;
                float gg = mx(grad, 1e-4f);
                pc *= 1 - clamp01(0.5f - mn(d1, d2) * rsqrt(gg * gg));
            }

            if (plain && cov >= 1 && shade >= 1 && (pc <= 0 || pc >= 1)) { /* the flat inside of the white or the pupil */
                buf[y * w + x] = pc >= 1 ? ink_pupil : ink_white; PE(3);
                continue;
            }
            if (plain && cov >= 1 && pc <= 0) { /* the shadow crescent: the white times the shade, dithered */
                static const float bayer16[4][4] = {{0.5f / 16, 8.5f / 16, 2.5f / 16, 10.5f / 16}, {12.5f / 16, 4.5f / 16, 14.5f / 16, 6.5f / 16},
                                                    {3.5f / 16, 11.5f / 16, 1.5f / 16, 9.5f / 16}, {15.5f / 16, 7.5f / 16, 13.5f / 16, 5.5f / 16}};
                uint16_t ec = f->color;
                float dz = bayer16[y & 3][x & 3];
                int rr = (int)(((ec >> 11) & 31) * shade + dz), gg_ = (int)(((ec >> 5) & 63) * shade + dz), bb = (int)((ec & 31) * shade + dz);
                buf[y * w + x] = swap_if((uint16_t)(rr << 11 | gg_ << 5 | bb), sw); PE(3);
                continue;
            }
            uint16_t ecol = f->color, pcol = pupil_color;
            float rr2 = e->veins > 0 ? p.x * p.x + p.y * p.y : 0;
            if (rr2 > 0.42f * 0.42f) { /* veins live on the ball (p), so they turn with it; they start at the rim (0.42) */
                float ang = atan2_fast(p.y, p.x), rr_ = sqrt_fast(rr2), R = mn(a, b);
                float v1 = ang * 1.114f + 0.18f * sin_fast(rr_ * 13 + ang * 5);        /* 7 main veins */
                float v2 = ang * 2.069f + 0.3f * sin_fast(rr_ * 21 + ang * 2) + 0.5f;  /* 13 short branches */
                float d1 = off_int(v1) * 0.898f * rr_ * R, d2 = off_int(v2) * 0.483f * rr_ * R;
                float m1 = clamp01(0.6f + 0.9f * (rr_ - 0.45f) - d1) * clamp01((rr_ - 0.42f) * 4);
                float m2 = clamp01(0.4f - d2) * clamp01((rr_ - 0.72f) * 5);
                ecol = mix565(ecol, f->vein_color, clamp01(e->veins) * 0.85f * mx(m1, m2));
            }
            if (e->wet > 0 && !plain) { /* a pool of tears along the lower lid with a bright meniscus on top */
                float top = ext_y * (1 - 0.34f * clamp01(e->wet)) + 1.5f * sin_fast(eu * 0.08f + e->wob_t * 2);
                float pool = clamp01(ev - top + 0.5f) * 0.5f * clamp01(e->wet), rim = clamp01(1.6f - fabsf(ev - top - 0.8f)) * clamp01(e->wet);
                ecol = mix565(mix565(ecol, f->water_color, pool), 0xFFFF, rim);
                pcol = mix565(mix565(pcol, f->water_color, pool), 0xFFFF, rim);
            }
            buf[y * w + x] = swap_if(pixel(ecol, pcol, shade, pc, cov, swap_if(buf[y * w + x], sw), x, y), sw); PE(3);
        }
    }
}

/* soft pink oval under the eye, drawn before (behind) the eyes */
static face_rect_t blush_rect(const eye_t *e, int w, int h, float *rx, float *ry, float *cx, float *cy)
{
    *rx = e->w * 0.36f, *ry = e->h * 0.1f + 5, *cx = e->x, *cy = e->y + e->h * 0.5f + *ry * 0.4f;
    return (face_rect_t){maxi(0, (int)(*cx - *rx)), maxi(0, (int)(*cy - *ry)), mini(w - 1, (int)(*cx + *rx) + 1), mini(h - 1, (int)(*cy + *ry) + 1)};
}

FACE_HOT static void blush_rows(const face_t *f, const eye_t *e, uint16_t *buf, int w, int h, int y0, int y1)
{
    float rx, ry, cx, cy;
    face_rect_t r = blush_rect(e, w, h, &rx, &ry, &cx, &cy);
    float irx = 1.0f / rx, iry = 1.0f / ry;
    for (int y = maxi(r.y1, y0); y <= r.y2 && y < y1; y++)
        for (int x = r.x1; x <= r.x2; x++) {
            float nx = (x + 0.5f - cx) * irx, ny = (y + 0.5f - cy) * iry;
            float a = f->blush * 0.9f * clamp01((1 - sqrt_fast(nx * nx + ny * ny)) * 2.0f);
            if (a > 0) buf[y * w + x] = swap_if(pixel(f->blush_color, 0, 1, 0, a, swap_if(buf[y * w + x], f->swapped), x, y), f->swapped);
        }
}

/* one frame: what to clear, what to paint, in which order; shared by the bands */
typedef struct {
    face_t *f;
    uint16_t *buf;
    int w, h, first, y_start;
    bool blush;
    face_rect_t clear[4];
    eye_setup_t eye[2];
} frame_t;

FACE_HOT static void frame_rows(void *ctx, int y0, int y1)
{
    static band_t bands[2]; /* the band that starts the frame gets the first */
    frame_t *fr = ctx;
    band_t *band = &bands[y0 == fr->y_start ? 0 : 1];
    PS
    for (int i = 0; i < 4; i++) { /* last frame's eyes and cheeks go first */
        face_rect_t r = fr->clear[i];
        for (int y = maxi(r.y1, y0); r.x2 >= r.x1 && y <= r.y2 && y < y1; y++) memset(&fr->buf[y * fr->w + r.x1], 0, (r.x2 - r.x1 + 1) * 2);
    }
    PE(4);
    {
    PS
    if (fr->blush)
        for (int i = 0; i < 2; i++) blush_rows(fr->f, &fr->f->eye[i], fr->buf, fr->w, fr->h, y0, y1);
    PE(5);
    }
    {
    PS
    for (int n = 0; n < 2; n++) { /* the smaller (farther) eye first, so the near one overlaps it */
        int i = n ? 1 - fr->first : fr->first;
        eye_rows(fr->f, &fr->f->eye[i], &fr->eye[i], fr->buf, fr->w, y0, y1, band);
    }
    PE(6);
    }
}

void (*face_parallel)(void (*job)(void *ctx, int y0, int y1), void *ctx, int y0, int y1);
void (*face_start)(face_t *f, uint16_t *buf, int w, int h);
volatile int face_busy;

face_rect_t face_draw(face_t *f, uint16_t *buf, int w, int h)
{
    static frame_t fr; /* one frame at a time */
    face_rect_t dirty = {0, 0, -1, -1}, none = {0, 0, -1, -1};
    fr.f = f, fr.buf = buf, fr.w = w, fr.h = h;
    fr.blush = f->blush > 0.01f;
    fr.first = f->eye[0].w * f->eye[0].h < f->eye[1].w * f->eye[1].h ? 0 : 1;
    for (int i = 0; i < 4; i++) fr.clear[i] = f->drawn[i], dirty = rect_union(dirty, f->drawn[i]);
    for (int i = 0; i < 2; i++) {
        float rx, ry, cx, cy;
        f->drawn[2 + i] = fr.blush ? blush_rect(&f->eye[i], w, h, &rx, &ry, &cx, &cy) : none;
        eye_prepare(f, &f->eye[i], w, h, &fr.eye[i]);
        f->drawn[i] = fr.eye[i].r;
    }
    for (int i = 0; i < 4; i++) dirty = rect_union(dirty, f->drawn[i]);
    if (dirty.x2 < dirty.x1) return dirty;
    fr.y_start = dirty.y1;
    if (face_parallel) face_parallel(frame_rows, &fr, dirty.y1, dirty.y2 + 1);
    else frame_rows(&fr, dirty.y1, dirty.y2 + 1);
    return dirty;
}
