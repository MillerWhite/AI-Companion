/* Headless recorder for the KeyPet UI: deterministic runs saved to PNG/GIF/MP4 through ffmpeg.
   The interactive simulator is the web one (sim/web).

   pipsim --out a.gif [--secs 4] [--fps 30] [--events "0.5:k2 1:d100,200 1.3:m300,200 1.5:u 2:a0,0,-1"]
   events: <sec>:k<key>         pip_key
                 d<x>,<y>       finger down    m<x>,<y> move    u  up
                 a<ax>,<ay>,<az> accelerometer (g, device axes), held until changed
                 S / W[g]       toggle shaking / walking on top of it (W0.6: a hard walk, 0.6 g bounce)
                 V<db>          room noise at about this many dB SPL; Q toggles someone blowing on the mic
                 J<pct>[+]      the battery level, + while charging
                 p<id>          side button (0 PWR, 1 BOOT)
                 n<name>        pin a mood by name, e.g. nrage (A turns the automatic mood back on)
                 T<hours>       jump the wall clock (the run starts at 2026-10-10 12:00 UTC)
                 F / G          feed / play
                 o<app>         open an app (clock, care, qr, timer, torch, ball, notify, music, camera)
                 P / B          top (PWR) / bottom (BOOT) side button; B<s> holds the bottom one s seconds
                 Y              next menu style (honeycomb, carousel, list)
                 H<n>           theme: 0 Тишина, 1 Приборка, 2 Apple, 3 Диагностика
                 K<knob>=<v>    set a mood-tree knob (pet_knobs[] in ui/pet.c), e.g. Kmeet_shy=1
                 R[name]        a rare card now (stoned or pinkeye; random without a name)
                 L<0|1|2> N C<0|1> M<0|1>  the fake iPhone: link off/pairing/on, a notification, a call, music
                 X<0|1|2>       a fake computer for the remote: gone, asks to pair, paired and back
                 g<gx>,<gy>,<gz> gyroscope, deg/s, device axes, held until changed
                 Z              "Обновление" on top: the panel is about to flash
   .png saves the last frame. Prints pip's status at the end. --wav out.wav also records Pip's voice (16 kHz mono). */
#include "fakephone.h"
#include "mic.h"
#include "lvgl.h"
#include "pet.h"
#include "pip.h"
#include "shell.h"
#include "theme.h"
#include "voice.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define S 466

static uint16_t fb[S * S];
static uint8_t rgb[S * S * 3];
static struct { bool down; int x, y; } ptr;
static float acc[3] = {0, 1, 0}; /* held upright, screen facing you */
static float gyro[3]; /* deg/s, held until changed */
static float boot_hold; /* s until the held bottom key is let go */
static bool shaking, walking;
static float walk_g; /* W<g>: the bounce of a walk, 0.35 g if not given */
static float room_db = -1; /* V<db>: the pretend mic hears white noise this loud; < 0: no mic */
static bool blowing;

/* the pretend microphone: white noise for the room, plus loud low rumble while someone blows */
static void fake_mic(float dt)
{
    if (room_db < 0) return;
    static int16_t buf[4096];
    static float rumble;
    int n = (int)(dt * 16000);
    n = n > 4096 ? 4096 : n;
    float a = powf(10, (room_db - 96) / 20) * 1.732f * 32768; /* uniform noise of that RMS */
    for (int i = 0; i < n; i++) {
        float x = a * (rand() / (float)RAND_MAX * 2 - 1);
        rumble += ((rand() / (float)RAND_MAX * 2 - 1) * 30000 - rumble) * 0.04f; /* noise low-passed near 100 Hz */
        if (blowing) x += rumble * 2.5f;
        buf[i] = (int16_t)fmaxf(-32768, fminf(32767, x));
    }
    mic_feed(buf, n, 16000);
}
static double clock_jump; /* s */
#define START 1791633600LL /* 2026-10-10 12:00 UTC: runs are reproducible */

static void flush_cb(lv_display_t *d, const lv_area_t *a, uint8_t *px) { (void)a; (void)px; lv_display_flush_ready(d); }

static void read_cb(lv_indev_t *i, lv_indev_data_t *d)
{
    (void)i;
    d->point.x = ptr.x;
    d->point.y = ptr.y;
    d->state = ptr.down ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

/* RGB565 (byte-swapped, as the board's panel takes it) -> RGB24, with the round glass masked and a grey bezel around it */
static void to_rgb(void)
{
    for (int y = 0; y < S; y++)
        for (int x = 0; x < S; x++) {
            float dx = x + 0.5f - S / 2.0f, dy = y + 0.5f - S / 2.0f;
            float in = pip_display_on() ? fminf(fmaxf(S / 2.0f - sqrtf(dx * dx + dy * dy) + 0.5f, 0), 1) : 0; /* off: all bezel-dark */
            uint16_t c = (uint16_t)(fb[y * S + x] << 8 | fb[y * S + x] >> 8);
            float r = ((c >> 11) & 31) * 255 / 31, g = ((c >> 5) & 63) * 255 / 63, b = (c & 31) * 255 / 31;
            uint8_t *o = &rgb[(y * S + x) * 3];
            o[0] = (uint8_t)(r * in + 42 * (1 - in));
            o[1] = (uint8_t)(g * in + 42 * (1 - in));
            o[2] = (uint8_t)(b * in + 48 * (1 - in));
        }
}

static void run_event(const char *ev)
{
    int x, y;
    if (ev[0] == 'k') pip_key(ev[1]);
    else if (ev[0] == 'u') ptr.down = false;
    else if (ev[0] == 'S') shaking = !shaking;
    else if (ev[0] == 'V') room_db = (float)atof(ev + 1);
    else if (ev[0] == 'Q') blowing = !blowing, room_db = room_db < 0 ? 30 : room_db;
    else if (ev[0] == 'J') shell_battery(atoi(ev + 1), strchr(ev, '+') != NULL);
    else if (ev[0] == 'W') walking = !walking, walk_g = (float)atof(ev + 1);
    else if (ev[0] == 'p') shell_button(atoi(ev + 1)), shell_button_up(atoi(ev + 1));
    else if (ev[0] == 'n' && pip_find_mood(ev + 1) >= 0) pip_set_mood(pip_find_mood(ev + 1));
    else if (ev[0] == 'A') pip_auto(true);
    else if (ev[0] == 'U') pip_wake_up();
    else if (ev[0] == 'D') pip_dev(atoi(ev + 1) != 0); /* D1: developer mode on */
    else if (ev[0] == 'o') shell_open(ev + 1);
    else if (ev[0] == 'B') { /* pressed, and let go now or after B<s> seconds */
        shell_button(PIP_BTN_BOOT);
        if ((boot_hold = (float)atof(ev + 1)) <= 0) shell_button_up(PIP_BTN_BOOT);
    }
    else if (ev[0] == 'P') shell_button(PIP_BTN_PWR);
    else if (ev[0] == 'Z') shell_updating();
    else if (ev[0] == 'Y') shell_set_menu_style((shell_menu_style() + 1) % 3);
    else if (ev[0] == 'H') shell_set_theme(atoi(ev + 1));
    else if (ev[0] == 'L' || ev[0] == 'N' || ev[0] == 'C' || ev[0] == 'M') fake_phone(ev[0], atoi(ev + 1)); /* the fake iPhone */
    else if (ev[0] == 'T') clock_jump += atof(ev + 1) * 3600;
    else if (ev[0] == 'F') pip_action(PIP_ACT_FEED);
    else if (ev[0] == 'G') pip_action(PIP_ACT_PLAY);
    else if (ev[0] == 'R') pet_rare_now(pip_pet(), pip_find_mood(ev + 1));
    else if (ev[0] == 'K' && strchr(ev, '=')) {
        char name[32];
        snprintf(name, sizeof name, "%.*s", (int)(strchr(ev, '=') - ev - 1), ev + 1);
        float *k = pet_knob(name);
        if (k) *k = (float)atof(strchr(ev, '=') + 1);
        else fprintf(stderr, "no knob %s\n", name);
    }
    else if (ev[0] == 'a' && sscanf(ev + 1, "%f,%f,%f", &acc[0], &acc[1], &acc[2]) == 3) {}
    else if (ev[0] == 'g' && sscanf(ev + 1, "%f,%f,%f", &gyro[0], &gyro[1], &gyro[2]) == 3) {}
    else if (ev[0] == 'X') fake_host(atoi(ev + 1));
    else if ((ev[0] == 'd' || ev[0] == 'm') && sscanf(ev + 1, "%d,%d", &x, &y) == 2) {
        ptr.x = x;
        ptr.y = y;
        ptr.down = ev[0] == 'd' || ptr.down;
    } else fprintf(stderr, "bad event: %s\n", ev);
}

int main(int argc, char **argv)
{
    const char *out = NULL, *wav = NULL;
    char *events = NULL;
    float secs = 4;
    int fps = 30;
    for (int i = 1; i + 1 < argc; i += 2) {
        if (!strcmp(argv[i], "--out")) out = argv[i + 1];
        else if (!strcmp(argv[i], "--secs")) secs = atof(argv[i + 1]);
        else if (!strcmp(argv[i], "--fps")) fps = atoi(argv[i + 1]);
        else if (!strcmp(argv[i], "--events")) events = argv[i + 1];
        else if (!strcmp(argv[i], "--wav")) wav = argv[i + 1];
        else { fprintf(stderr, "unknown option %s\n", argv[i]); return 2; }
    }
    if (argc == 2 && !strcmp(argv[1], "--list")) { /* every mood, and fail if one has no table entry */
        for (int i = 0; i < PIP_MOOD_COUNT; i++) {
            if (!pip_mood_name(i)) { fprintf(stderr, "mood %d has no entry\n", i); return 1; }
            puts(pip_mood_name(i));
        }
        return 0;
    }
    if (!out) { fprintf(stderr, "usage: pipsim --out file.png|gif|mp4 [--secs N] [--fps N] [--events ...]\n"); return 2; }

    lv_init();
    lv_rand_set_seed(1);
    voice_init(16000);
    static int16_t audio[16000 * 120];
    int audio_n = 0;
    lv_display_t *disp = lv_display_create(S, S);
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565_SWAPPED); /* as the board draws: the same code paths */
    lv_display_set_buffers(disp, fb, NULL, sizeof fb, LV_DISPLAY_RENDER_MODE_DIRECT);
    lv_display_set_flush_cb(disp, flush_cb);
    lv_indev_t *indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, read_cb);
    shell_create();
    fake_phone_init();
    shell_battery(82, false); /* a pretend battery */

    const char *ext = strrchr(out, '.');
    bool png = ext && !strcmp(ext, ".png");
    const char *vf = ext && !strcmp(ext, ".gif") ? "-vf \"split[a][b];[a]palettegen[p];[b][p]paletteuse\""
                     : png                      ? ""
                                                : "-pix_fmt yuv420p";
    char cmd[1024];
    snprintf(cmd, sizeof cmd, "ffmpeg -loglevel error -y -f rawvideo -pix_fmt rgb24 -s %dx%d -r %d -i - %s '%s'", S, S,
             fps, vf, out);
    FILE *ff = popen(cmd, "w");
    if (!ff) { perror("ffmpeg"); return 1; }

    int frames = (int)lroundf(secs * fps);
    char *save = NULL, *ev = events ? strtok_r(events, " ", &save) : NULL;
    uint32_t now = 0;
    double busy = 0;
    for (int f = 0; f <= frames; f++) {
        uint32_t t = (uint32_t)((double)f * 1000 / fps);
        float dt = (t - now) / 1000.0f, ts = t / 1000.0f;
        lv_tick_inc(t - now);
        now = t;
        while (ev && atof(ev) * 1000 <= now) {
            run_event(strchr(ev, ':') ? strchr(ev, ':') + 1 : ev);
            ev = strtok_r(NULL, " ", &save);
        }
        float sx = shaking ? 2.5f * sinf(ts * 2 * 3.14159f * 7) : 0;
        float wy = walking ? (walk_g ? walk_g : 0.35f) * sinf(ts * 2 * 3.14159f * 1.8f) : 0; /* along gravity: held upright */
        shell_imu(acc[0] + sx, acc[1] + wy, acc[2], dt > 0 ? dt : 1.0f / fps);
        shell_gyro(gyro[0], gyro[1], gyro[2], dt > 0 ? dt : 1.0f / fps);
        if (boot_hold > 0 && (boot_hold -= dt) <= 0) shell_button_up(PIP_BTN_BOOT);
        fake_mic(dt);
        shell_time(START + (int64_t)(ts + clock_jump), 0);
        clock_t c0 = clock();
        lv_timer_handler();
        int want = (int)((int64_t)t * 16000 / 1000) - audio_n; /* the voice, in step with the frames */
        if (want > 0 && audio_n + want <= (int)(sizeof audio / 2)) voice_render(audio + audio_n, want), audio_n += want;
        busy += (double)(clock() - c0) / CLOCKS_PER_SEC;
        if (!png || f == frames) {
            to_rgb();
            fwrite(rgb, 1, sizeof rgb, ff);
        }
    }
    if (wav) { /* 16-bit mono WAV */
        FILE *f = fopen(wav, "wb");
        uint32_t data = (uint32_t)audio_n * 2, riff = 36 + data, fmt = 16, rate = 16000, bps = 32000;
        uint16_t pcmfmt = 1, ch = 1, align = 2, bits = 16;
        fwrite("RIFF", 1, 4, f), fwrite(&riff, 4, 1, f), fwrite("WAVEfmt ", 1, 8, f), fwrite(&fmt, 4, 1, f);
        fwrite(&pcmfmt, 2, 1, f), fwrite(&ch, 2, 1, f), fwrite(&rate, 4, 1, f), fwrite(&bps, 4, 1, f);
        fwrite(&align, 2, 1, f), fwrite(&bits, 2, 1, f), fwrite("data", 1, 4, f), fwrite(&data, 4, 1, f);
        fwrite(audio, 2, (size_t)audio_n, f);
        fclose(f);
    }
    pip_info_t in;
    pip_get_info(&in);
    printf("%s: %d frames, UI work %.2f ms/frame on this Mac\n", out, frames + 1, busy * 1000 / (frames + 1));
    printf("pet: hour=%d food=%.2f energy=%.2f fun=%.2f love=%.2f walk=%.2f day_mood=%.2f\n", in.hour, in.food, in.energy,
           in.fun, in.love, in.walk, in.day_mood);
    pet_why_t why;
    int base = pet_mood_why(pip_pet(), 0, &why);
    printf("status: mood=%s reaction=%s steps=%d face_down=%d asleep=%d auto=%d screen=%s theme=%d phone=%s shots=%d base=%s why=%d.%d display=%d %s\n",
           in.mood, in.reaction, in.steps, in.face_down, in.asleep, in.auto_mood, shell_where(), theme_index(),
           *fake_phone_last() ? fake_phone_last() : "-", fake_phone_shots(), pip_mood_name(base), why.kind, why.which, pip_display_on(), fake_host_report());
    return pclose(ff) ? 1 : 0;
}
