/* Renderer checks that pictures can't catch reliably: nothing is ever shaved off an eye. Run by sim/check.py. */
#include "face.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* not assert(): the build is Release, NDEBUG would compile it away */
#define CHECK(c, msg) do { if (!(c)) { fprintf(stderr, "FAIL: %s\n", msg); exit(1); } } while (0)

#define S 466
static uint16_t a[S * S], b[S * S];

static face_t eye(float lid, float smile, float wob_t)
{
    eye_t e = {.x = 233, .y = 233, .w = 171, .h = 200, .depth = 85, .lid = lid, .smile = smile,
               .pw = .4f, .ph = .44f, .wobble = 2.0f, .wob_t = wob_t};
    face_t f = {.eye = {e, e}, .color = 0xFFFF, .pupil_color = 0x18C3, .shading = 1};
    f.eye[1].x = 1000; /* off screen */
    return f;
}

int main(void)
{
    /* a lid or smile spring settling around zero must not cut anything */
    face_t f0 = eye(0, 0, 0), f1 = eye(0.03f, 0.03f, 0);
    memset(a, 0, sizeof a), memset(b, 0, sizeof b);
    face_draw(&f0, a, S, S);
    face_draw(&f1, b, S, S);
    CHECK(!memcmp(a, b, sizeof a), "tiny lid/smile changed the eye");

    /* the living outline must stay inside the box the renderer paints (and clears next frame) */
    for (float t = 0; t < 20; t += 0.37f) {
        face_t f = eye(0, 0, t);
        memset(a, 0, sizeof a);
        face_draw(&f, a, S, S);
        face_rect_t r = f.drawn[0];
        for (int x = r.x1; x <= r.x2; x++) CHECK(!a[r.y1 * S + x] && !a[r.y2 * S + x], "eye touches its box: clipped");
        for (int y = r.y1; y <= r.y2; y++) CHECK(!a[y * S + r.x1] && !a[y * S + r.x2], "eye touches its box: clipped");
    }

    /* drawn in the panel's byte order it is the same picture, every pixel byte-swapped: the blends, the blush, tears,
       veins and the heart included (they read back what is under them) */
    for (int k = 0; k < 2; k++) {
        face_t p = eye(0.2f, 0.1f, 1.3f), q;
        p.shading = (float)k, p.blush = 0.6f, p.blush_color = 0xFBD4, p.vein_color = 0xD9A7, p.water_color = 0xAEDF, p.heart_color = 0xFA2F;
        p.eye[0].glint = 1, p.eye[0].wet = k, p.eye[0].veins = k, p.eye[0].heart = 1 - k;
        q = p, q.swapped = 1;
        for (int i = 0; i < S * S; i++) a[i] = (uint16_t)(i * 2654435761u >> 16), b[i] = (uint16_t)(a[i] << 8 | a[i] >> 8); /* a busy background */
        face_draw(&p, a, S, S);
        face_draw(&q, b, S, S);
        for (int i = 0; i < S * S; i++) CHECK(b[i] == (uint16_t)(a[i] << 8 | a[i] >> 8), "byte-swapped drawing differs");
    }
    puts("face_test ok");
    return 0;
}
