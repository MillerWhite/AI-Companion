/* WebAssembly host for the web simulator (sim/web/index.html): the page drives time, touch, IMU and buttons. */
#include "fakephone.h"
#include "lvgl.h"
#include "pip.h"
#include "shell.h"
#include "theme.h"
#include "voice.h"
#include <emscripten.h>
#include <stdio.h>
#include <string.h>

#define S 466

static uint16_t fb[S * S];
static struct { bool down; int x, y; } ptr;

static void flush_cb(lv_display_t *d, const lv_area_t *a, uint8_t *px) { (void)a; (void)px; lv_display_flush_ready(d); }

static void read_cb(lv_indev_t *i, lv_indev_data_t *d)
{
    (void)i;
    d->point.x = ptr.x;
    d->point.y = ptr.y;
    d->state = ptr.down ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

EMSCRIPTEN_KEEPALIVE void sim_init(unsigned seed)
{
    lv_init();
    lv_rand_set_seed(seed);
    lv_display_t *disp = lv_display_create(S, S);
    lv_display_set_buffers(disp, fb, NULL, sizeof fb, LV_DISPLAY_RENDER_MODE_DIRECT);
    lv_display_set_flush_cb(disp, flush_cb);
    lv_indev_t *indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, read_cb);
    shell_create();
    fake_phone_init();
    shell_battery(82, false); /* a pretend battery */
}

EMSCRIPTEN_KEEPALIVE uint16_t *sim_fb(void) { return fb; }
EMSCRIPTEN_KEEPALIVE void sim_frame(int ms) { lv_tick_inc(ms); lv_timer_handler(); }
EMSCRIPTEN_KEEPALIVE void sim_touch(int x, int y, int down) { ptr.x = x; ptr.y = y; ptr.down = down; }
EMSCRIPTEN_KEEPALIVE void sim_imu(float ax, float ay, float az, float dt) { shell_imu(ax, ay, az, dt); }
EMSCRIPTEN_KEEPALIVE void sim_gyro(float gx, float gy, float gz, float dt) { shell_gyro(gx, gy, gz, dt); }
EMSCRIPTEN_KEEPALIVE int sim_display_on(void) { return pip_display_on(); }
EMSCRIPTEN_KEEPALIVE void sim_button(int id) { shell_button(id); }
EMSCRIPTEN_KEEPALIVE void sim_button_up(int id) { shell_button_up(id); }
EMSCRIPTEN_KEEPALIVE void sim_key(int k) { pip_key((char)k); }
EMSCRIPTEN_KEEPALIVE void sim_time(double unix_s, int tz_min) { shell_time((int64_t)unix_s, tz_min); }
EMSCRIPTEN_KEEPALIVE void sim_action(int act) { pip_action(act); }
EMSCRIPTEN_KEEPALIVE void sim_auto(int on) { pip_auto(on); }
/* persistence: JS copies the pet state to localStorage and back through this buffer */
static uint8_t pet_buf[256];
EMSCRIPTEN_KEEPALIVE uint8_t *sim_pet_buf(void) { return pet_buf; }
EMSCRIPTEN_KEEPALIVE int sim_pet_save(void)
{
    size_t n;
    const void *p = pip_pet_state(&n);
    memcpy(pet_buf, p, n);
    return (int)n;
}
EMSCRIPTEN_KEEPALIVE int sim_pet_restore(int n) { return pip_pet_restore(pet_buf, (size_t)n); }
/* audio: the page's AudioContext pulls Pip's voice at its own sample rate */
static int16_t audio_buf[8192];
EMSCRIPTEN_KEEPALIVE void sim_voice_init(int rate) { voice_init(rate); }
EMSCRIPTEN_KEEPALIVE int16_t *sim_audio(int n) { voice_render(audio_buf, n > 8192 ? 8192 : n); return audio_buf; }
EMSCRIPTEN_KEEPALIVE void sim_say(int sound) { voice_say(sound); }
EMSCRIPTEN_KEEPALIVE int sim_voice_count(void) { return VOICE_COUNT; }
EMSCRIPTEN_KEEPALIVE const char *sim_voice_name(int i) { return voice_name(i); }
EMSCRIPTEN_KEEPALIVE void sim_volume(float v) { voice_set_volume(v); }
EMSCRIPTEN_KEEPALIVE float sim_get_volume(void) { return voice_volume(); }
EMSCRIPTEN_KEEPALIVE void sim_set_menu_style(int style) { shell_set_menu_style(style); }
EMSCRIPTEN_KEEPALIVE int sim_menu_style(void) { return shell_menu_style(); }
EMSCRIPTEN_KEEPALIVE void sim_set_theme(int t) { shell_set_theme(t); }
EMSCRIPTEN_KEEPALIVE int sim_theme(void) { return theme_index(); }
EMSCRIPTEN_KEEPALIVE int sim_mood_count(void) { return PIP_MOOD_COUNT; }
EMSCRIPTEN_KEEPALIVE void sim_set_mood(int i) { pip_set_mood(i); }
EMSCRIPTEN_KEEPALIVE const char *sim_mood_name(int i) { return i >= 0 && i < PIP_MOOD_COUNT ? pip_mood_name(i) : ""; }

/* the fake iPhone: 'L' link 0/1/2, 'N' notification, 'C' call 1/0, 'M' music 1/0; 'X' the fake computer 0/1/2 */
EMSCRIPTEN_KEEPALIVE void sim_phone(int kind, int arg)
{
    if (kind == 'X') fake_host(arg);
    else fake_phone((char)kind, arg);
}
EMSCRIPTEN_KEEPALIVE const char *sim_phone_last(void) { return fake_phone_last(); }
EMSCRIPTEN_KEEPALIVE const char *sim_host_report(void) { return fake_host_report(); }
EMSCRIPTEN_KEEPALIVE int sim_phone_shots(void) { return fake_phone_shots(); }

EMSCRIPTEN_KEEPALIVE const char *sim_status(void)
{
    static char buf[512];
    pip_info_t in;
    pip_get_info(&in);
    snprintf(buf, sizeof buf,
             "{\"mood\":\"%s\",\"reaction\":\"%s\",\"steps\":%d,\"tilt\":[%.2f,%.2f],\"shake\":%.2f,\"faceDown\":%s,"
             "\"food\":%.3f,\"energy\":%.3f,\"fun\":%.3f,\"love\":%.3f,\"walk\":%.3f,\"dayMood\":%.2f,"
             "\"asleep\":%s,\"auto\":%s,\"hour\":%d,\"screen\":\"%s\"}",
             in.mood, in.reaction, in.steps, in.tilt_x, in.tilt_y, in.shake, in.face_down ? "true" : "false",
             in.food, in.energy, in.fun, in.love, in.walk, in.day_mood, in.asleep ? "true" : "false",
             in.auto_mood ? "true" : "false", in.hour, shell_where());
    return buf;
}
