/* Voice checks: every sound is audible, ends, and sits where it should in pitch. With a path, also writes
   all of them in a row to a WAV to listen to. Run by sim/check.py. */
#include "voice.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(c, msg, ...) do { if (!(c)) { fprintf(stderr, "FAIL: " msg "\n", ##__VA_ARGS__); exit(1); } } while (0)
#define RATE 16000
static int16_t pcm[RATE * 60];

/* renders one sound; returns its length in samples, and its zero-crossing pitch estimate */
static int render(int sound, int16_t *out, float *pitch)
{
    voice_say(sound);
    int n = 0, crossings = 0, voiced = 0;
    while (voice_busy() && n < RATE * 5) {
        voice_render(out + n, 160);
        for (int i = n; i < n + 160; i++) {
            if (abs(out[i]) > 800) voiced++;
            if (i > 0 && (out[i - 1] < 0) != (out[i] < 0) && abs(out[i]) + abs(out[i - 1]) > 400) crossings++;
        }
        n += 160;
    }
    *pitch = voiced ? crossings / 2.0f / (voiced / (float)RATE) : 0;
    return n;
}

int main(int argc, char **argv)
{
    voice_init(RATE);
    float pitch[VOICE_COUNT];
    int total = 0;
    for (int s = 0; s < VOICE_COUNT; s++) {
        int n = render(s, pcm + total, &pitch[s]);
        double e = 0;
        for (int i = 0; i < n; i++) e += (double)pcm[total + i] * pcm[total + i];
        float rms = sqrtf((float)(e / n));
        printf("%-7s %5.2f s  rms %5.0f  pitch ~%4.0f Hz\n", voice_name(s), n / (float)RATE, rms, pitch[s]);
        CHECK(rms > 1000, "%s is too quiet", voice_name(s));
        CHECK(n < RATE * 3, "%s does not end", voice_name(s));
        total += n;
        memset(pcm + total, 0, RATE / 2 * 2); /* half a second between sounds */
        total += RATE / 2;
    }
    CHECK(pitch[VOICE_HAPPY] > pitch[VOICE_SAD] * 1.4f, "happy should sit well above sad");
    CHECK(pitch[VOICE_GROWL] < pitch[VOICE_HAPPY] * 0.7f, "a growl is low");
    voice_set_volume(0);
    int16_t quiet[1600];
    voice_say(VOICE_HAPPY);
    bool any = false;
    for (int i = 0; i < 20; i++) any |= voice_render(quiet, 1600);
    CHECK(!any, "mute is silent");

    if (argc > 1) { /* 16-bit mono WAV */
        FILE *f = fopen(argv[1], "wb");
        uint32_t data = (uint32_t)total * 2, riff = 36 + data, fmt = 16, rate = RATE, bps = RATE * 2;
        uint16_t pcmfmt = 1, ch = 1, align = 2, bits = 16;
        fwrite("RIFF", 1, 4, f), fwrite(&riff, 4, 1, f), fwrite("WAVEfmt ", 1, 8, f), fwrite(&fmt, 4, 1, f);
        fwrite(&pcmfmt, 2, 1, f), fwrite(&ch, 2, 1, f), fwrite(&rate, 4, 1, f), fwrite(&bps, 4, 1, f);
        fwrite(&align, 2, 1, f), fwrite(&bits, 2, 1, f), fwrite("data", 1, 4, f), fwrite(&data, 4, 1, f);
        fwrite(pcm, 2, (size_t)total, f);
        fclose(f);
    }
    puts("voice_test ok");
    return 0;
}
