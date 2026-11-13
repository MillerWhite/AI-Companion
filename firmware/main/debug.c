/* The debug link over USB: a status line a second and the day's history out, text commands in.
   tools/pipdebug.py talks to it. Lines out start with '@' so they stand out of the log:
     @S {json}          now: mood, why, needs, valence, features, the worst frame of the last 5 s (fw)...
     @H t mood f e fu l w day v asleep why   one history row (a minute each, the last 24 h; kept in flash)
     @N a,b,c           mood names, in order
     @K name value min max   one knob of the mood tree (pet_knobs[]), a line each, then
     @T {json}          the urges [need, line, mood, other face]
   why = [kind, which, value]: the branch of the tree that picked the mood (ui/pet.h pet_mood_why).
   Commands in: mood <i> | auto | react <i> <s> | need <0-4> <v> | day <v> | annoy <v> | urge <i> <line>
                pool <great> <good> <meh> | set <knob> <v> | feat <f> <0|1> | demo <s> [i ...] | hist | names | tune
                open <app> | home | btn <0|1> | meet | feed | play | sleep | wake | rare [mood] | game <0 won|1 lost|2 record> */
#include "board.h"
#include "driver/usb_serial_jtag.h"
#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "esp_partition.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "mic.h"
#include "pet.h"
#include "pip.h"
#include "profile.h"
#include "phone.h"
#include "shell.h"
#include "theme.h"
#include "shell.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define HIST_N (24 * 60)
typedef struct { int32_t t; uint8_t mood, need[5], why; int8_t day, v; uint8_t asleep, tag[2]; } hist_t; /* 16 bytes */
#define HIST_TAG(h) ((h).tag[0] == 'P' && (h).tag[1] == 'h') /* ours: the partition may hold another firmware's bytes */
_Static_assert(sizeof(hist_t) == 16, "a flash slot");
static hist_t *hist;
static int hist_n, hist_at;

/* In flash: a ring of 16-byte slots at the start of the "storage" partition, a 4 KB sector erased as the ring
   enters it (once every ~4 h: no wear to speak of). After a restart the newest 24 h come back in order. */
#define RING_SECTORS 6
#define RING_SLOTS (RING_SECTORS * 4096 / 16)
static const esp_partition_t *ring;
static int ring_next;

static void hist_load(void)
{
    if (!(ring = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "storage"))) return;
    hist_t *all = heap_caps_malloc(RING_SLOTS * sizeof *all, MALLOC_CAP_SPIRAM);
    if (!all || esp_partition_read(ring, 0, all, RING_SLOTS * sizeof *all) != ESP_OK) { free(all); return; }
    int newest = -1;
    for (int i = 0; i < RING_SLOTS; i++)
        if (HIST_TAG(all[i]) && all[i].t > 1735689600 && (newest < 0 || all[i].t > all[newest].t)) newest = i;
    ring_next = newest < 0 ? 0 : (newest + 1) % RING_SLOTS;
    /* the slots before the newest, oldest first, as long as time runs forward */
    int n = 0, start = newest;
    while (newest >= 0 && n + 1 < HIST_N) {
        int prev = (start - 1 + RING_SLOTS) % RING_SLOTS;
        if (n + 1 >= RING_SLOTS || !HIST_TAG(all[prev]) || all[prev].t <= 1735689600 || all[prev].t >= all[start].t) break;
        start = prev, n++;
    }
    if (newest >= 0 && (hist = heap_caps_calloc(HIST_N, sizeof *hist, MALLOC_CAP_SPIRAM)))
        for (int k = 0; k <= n; k++) hist[hist_n++] = all[(start + k) % RING_SLOTS], hist_at = hist_n % HIST_N;
    free(all);
}

static void ring_put(const hist_t *h)
{
    if (!ring || h->t <= 1735689600) return; /* no real time yet: nothing worth keeping */
    if (ring_next % 256 == 0) esp_partition_erase_range(ring, (size_t)ring_next * 16, 4096);
    esp_partition_write(ring, (size_t)ring_next * 16, h, sizeof *h);
    ring_next = (ring_next + 1) % RING_SLOTS;
}
static int demo_s, demo_list[PIP_MOOD_COUNT], demo_n, demo_k;
static int64_t demo_next;

static QueueHandle_t ble_lines; /* commands from the BLE link, run here rather than in the BLE host task */

static void put(const char *s) /* to USB, and to the page over Bluetooth when it is there */
{
    fputs(s, stdout), fflush(stdout);
    if (ble_debug_on()) ble_debug_send(s, (int)strlen(s));
}

void debug_line(char *line)
{
    char l[160];
    snprintf(l, sizeof l, "%s", line);
    if (ble_lines) xQueueSend(ble_lines, l, 0);
}

static void status(void)
{
    pip_info_t in;
    pip_get_info(&in);
    pet_t *p = pip_pet();
    pet_why_t w;
    pet_mood_why(p, tz_min, &w);
    bool ch;
    int bat = shell_battery_level(&ch);
    unsigned feat = 0;
    for (int f = 0; f < PIP_F_COUNT; f++) feat |= pip_feature_on(f) << f;
    const char *fw_mood, *fw_react;
    float fw = pip_frame_worst(5, &fw_mood, &fw_react); /* the slowest of Pip's frames lately */
    static char b[820]; /* static: the task's stack had ~0.5 KB to spare with it there */
    snprintf(b, sizeof b,
             "@S {\"m\":\"%s\",\"mi\":%d,\"r\":\"%s\",\"auto\":%d,\"sl\":%d,\"n\":[%.3f,%.3f,%.3f,%.3f,%.3f],\"day\":%.3f,"
             "\"an\":%.2f,\"v\":%.3f,\"h\":%d,\"why\":[%d,%d,%.3f],\"st\":%d,\"bat\":%d,\"ch\":%d,\"db\":%.0f,\"fms\":%.1f,"
             "\"feat\":%u,\"scr\":\"%s\",\"t\":%lld,\"demo\":%d,\"af\":[%d,%d,%lld],\"rare\":[%d,%lld],\"un\":%.2f,\"wk\":%.1f,"
             "\"wake\":%lld,\"pool\":%d,\"hold\":[%d,%lld],\"fw\":[%.0f,\"%s\",\"%s\"]}\n",
             in.mood, pip_find_mood(in.mood), in.reaction, in.auto_mood, in.asleep, p->food, p->energy, p->fun, p->love, p->walk,
             p->day_mood, p->annoy, pet_valence(p), in.hour, w.kind, w.which, w.value, in.steps, bat, ch, mic_on() ? mic_db() : -1,
             in.frame_ms, feat, shell_where(), (long long)time(NULL), demo_s,
             p->after_mood, p->after_why, (long long)(p->after_until - p->last), p->rare_mood, (long long)(p->rare_at - p->last),
             p->unseen, p->stride, (long long)(p->woke_at ? p->last - p->woke_at : -1), p->pool, p->hold,
             (long long)(p->hold >= 0 ? p->last - p->hold_since : 0), fw, fw_mood, fw_react);
    put(b);
}

static void hist_add(void)
{
    if (!hist && !(hist = heap_caps_calloc(HIST_N, sizeof *hist, MALLOC_CAP_SPIRAM))) return;
    pip_info_t in;
    pip_get_info(&in);
    pet_t *p = pip_pet();
    pet_why_t w;
    pet_mood_why(p, tz_min, &w);
    hist_t *h = &hist[hist_at];
    *h = (hist_t){(int32_t)time(NULL), (uint8_t)pip_find_mood(in.mood), {0}, (uint8_t)(w.kind * 16 + w.which),
                  (int8_t)(p->day_mood * 100), (int8_t)(pet_valence(p) * 100), p->asleep, {'P', 'h'}};
    const float n[5] = {p->food, p->energy, p->fun, p->love, p->walk};
    for (int i = 0; i < 5; i++) h->need[i] = (uint8_t)(n[i] * 255);
    ring_put(h);
    hist_at = (hist_at + 1) % HIST_N;
    if (hist_n < HIST_N) hist_n++;
}

static void hist_dump(void)
{
    char b[96];
    for (int k = 0; k < hist_n; k++) {
        hist_t *h = &hist[(hist_at - hist_n + k + HIST_N) % HIST_N];
        snprintf(b, sizeof b, "@H %ld %d %d %d %d %d %d %d %d %d %d\n", (long)h->t, h->mood, h->need[0], h->need[1], h->need[2],
                 h->need[3], h->need[4], h->day, h->v, h->asleep, h->why);
        put(b);
        if (k % 32 == 31) vTaskDelay(1); /* let the USB drain */
    }
    put("@H end\n");
}

static void names(void)
{
    static char b[1200];
    int n = snprintf(b, sizeof b, "@N ");
    for (int i = 0; i < PIP_MOOD_COUNT && n < (int)sizeof b - 32; i++) n += snprintf(b + n, sizeof b - n, "%s%s", i ? "," : "", pip_mood_name(i));
    snprintf(b + n, sizeof b - n, "\n");
    put(b);
}

static void tune(void)
{
    char b[400];
    for (int i = 0; i < pet_knob_count; i++) {
        const pet_knob_t *k = &pet_knobs[i];
        snprintf(b, sizeof b, "@K %s %.4g %.4g %.4g\n", k->name, *k->v, k->min, k->max);
        put(b);
    }
    int n = snprintf(b, sizeof b, "@T {\"urges\":[");
    for (int i = 0; i < PET_URGES; i++) {
        const pet_urge_t *u = &pet_tune.urges[i];
        n += snprintf(b + n, sizeof b - n, "%s[%d,%.3f,%d,%d]", i ? "," : "", u->need, u->line, u->mood, u->alt);
    }
    snprintf(b + n, sizeof b - n, "]}\n");
    put(b);
}

static float clamp01(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }

static void json_str(char *b, size_t n, const char *s) /* a JSON string, quoted, with " and \\ escaped */
{
    size_t k = 0;
    if (k + 1 < n) b[k++] = '"';
    for (; *s && k + 3 < n; s++) {
        if (*s == '"' || *s == '\\') b[k++] = '\\';
        b[k++] = *s;
    }
    if (k + 1 < n) b[k++] = '"';
    b[k] = 0;
}

static void config(void) /* @C: everything the settings page shows */
{
    static char b[1600], t[260];
    int n = snprintf(b, sizeof b, "@C {\"ver\":\"%s\",\"power\":%d,\"bright\":%d,\"theme\":%d,\"privacy\":%d,\"feat\":[",
                     esp_app_get_description()->version, pip_power_after(), settings_brightness(), theme_index(), phone_privacy());
    for (int f = 0; f < PIP_F_COUNT; f++) n += snprintf(b + n, sizeof b - n, "%s%d", f ? "," : "", pip_feature_on(f));
    const char *names[3] = {profile.first, profile.last, profile.org}, *keys[3] = {"first", "last", "org"};
    n += snprintf(b + n, sizeof b - n, "]");
    for (int i = 0; i < 3; i++) json_str(t, sizeof t, names[i]), n += snprintf(b + n, sizeof b - n, ",\"%s\":%s", keys[i], t);
    n += snprintf(b + n, sizeof b - n, ",\"links\":[");
    for (int i = 0; i < PROFILE_LINKS; i++) {
        const profile_link_t *l = &profile.link[i];
        n += snprintf(b + n, sizeof b - n, "%s[", i ? "," : "");
        json_str(t, sizeof t, l->service), n += snprintf(b + n, sizeof b - n, "%s,", t);
        json_str(t, sizeof t, l->handle), n += snprintf(b + n, sizeof b - n, "%s,", t);
        json_str(t, sizeof t, l->url), n += snprintf(b + n, sizeof b - n, "%s]", t);
    }
    snprintf(b + n, sizeof b - n, "]}\n");
    put(b);
}

static void field(char *dst, size_t n, char **s) /* the next "|"-separated field of *s */
{
    char *e = strchr(*s, '|');
    if (e) *e = 0;
    snprintf(dst, n, "%s", *s);
    *s = e ? e + 1 : *s + strlen(*s);
}

static void command(char *line)
{
    /* the profile, with spaces in it: name first|last|org, link i service|handle|url (an empty url clears it) */
    if (!strncmp(line, "name ", 5) || !strncmp(line, "link ", 5)) {
        char *s = line + 5;
        ui_lock();
        if (line[0] == 'n') {
            field(profile.first, sizeof profile.first, &s), field(profile.last, sizeof profile.last, &s), field(profile.org, sizeof profile.org, &s);
        } else {
            int i = atoi(s);
            char *sp = strchr(s, ' ');
            if (i >= 0 && i < PROFILE_LINKS) {
                profile_link_t *l = &profile.link[i];
                s = sp ? sp + 1 : s + strlen(s);
                field(l->service, sizeof l->service, &s), field(l->handle, sizeof l->handle, &s), field(l->url, sizeof l->url, &s);
            }
        }
        profile_changed();
        ui_unlock();
        config();
        return;
    }
    char *arg[20];
    int n = 0;
    for (char *t = strtok(line, " \t\r"); t && n < 20; t = strtok(NULL, " \t\r")) arg[n++] = t;
    if (!n) return;
    const char *c = arg[0];
    float a1 = n > 1 ? strtof(arg[1], NULL) : 0, a2 = n > 2 ? strtof(arg[2], NULL) : 0;
    pet_t *p = pip_pet();
    if (!strcmp(c, "hist")) { hist_dump(); return; }
    if (!strcmp(c, "names")) { names(); return; }
    if (!strcmp(c, "tune")) { tune(); return; }
    if (!strcmp(c, "config")) { config(); return; }
    ui_lock();
    if (!strcmp(c, "mood") && n > 1) pip_set_mood((int)a1);
    else if (!strcmp(c, "auto")) pip_auto(true), demo_s = 0;
    else if (!strcmp(c, "react") && n > 1) pip_react((int)a1, n > 2 ? a2 : 3, pip_mood_name((int)a1));
    else if (!strcmp(c, "need") && n > 2) {
        float *v[5] = {&p->food, &p->energy, &p->fun, &p->love, &p->walk};
        if (a1 >= 0 && a1 < 5) *v[(int)a1] = clamp01(a2);
    } else if (!strcmp(c, "day") && n > 1) p->day_mood = a1 < -1 ? -1 : a1 > 1 ? 1 : a1;
    else if (!strcmp(c, "annoy") && n > 1) p->annoy = a1 < 0 ? 0 : a1;
    else if (!strcmp(c, "urge") && n > 2 && a1 >= 0 && a1 < PET_URGES) pet_tune.urges[(int)a1].line = a2 < 0.01f ? 0.01f : a2;
    else if (!strcmp(c, "pool") && n > 3) pet_tune.great = a1, pet_tune.good = a2, pet_tune.meh = strtof(arg[3], NULL);
    else if (!strcmp(c, "set") && n > 2 && pet_knob(arg[1])) *pet_knob(arg[1]) = a2;
    else if (!strcmp(c, "rare")) pet_rare_now(p, n > 1 ? pip_find_mood(arg[1]) : -1);
    else if (!strcmp(c, "game") && n > 1 && a1 >= 0 && a1 <= 2) pip_game((int)a1);
    else if (!strcmp(c, "feat") && n > 2) pip_feature((int)a1, a2 != 0), settings_save();
    else if (!strcmp(c, "power") && n > 1) pip_power((int)a1), settings_save();      /* seconds to sleep, 0 never */
    else if (!strcmp(c, "bright") && n > 1) settings_bright((int)a1), settings_save(); /* % */
    else if (!strcmp(c, "privacy") && n > 1) phone_set_privacy((int)a1), settings_save();
    else if (!strcmp(c, "theme") && n > 1) shell_set_theme((int)a1);                   /* kept by main.c theme_keep */
    else if (!strcmp(c, "demo")) {
        demo_s = (int)a1, demo_n = 0, demo_k = 0, demo_next = 0;
        for (int i = 2; i < n && demo_n < PIP_MOOD_COUNT; i++) demo_list[demo_n++] = atoi(arg[i]);
        if (!demo_n) for (int i = 0; i < PIP_MOOD_COUNT; i++) demo_list[demo_n++] = i;
        if (!demo_s) pip_auto(true);
        else shell_home();
    } else if (!strcmp(c, "open") && n > 1) shell_open(arg[1]);
    else if (!strcmp(c, "home")) shell_home();
    else if (!strcmp(c, "btn") && n > 1) shell_button((int)a1), shell_button_up((int)a1); /* a press, let go at once */
    else if (!strcmp(c, "meet")) pip_meet();
    else if (!strcmp(c, "update")) shell_updating(); /* the panel flashes next */
    else if (!strcmp(c, "dev")) pip_dev(n > 1 ? a1 != 0 : !pip_dev_on()); /* the developer overlay */
    else if (!strcmp(c, "feed")) pip_action(PIP_ACT_FEED);
    else if (!strcmp(c, "play")) pip_action(PIP_ACT_PLAY);
    else if (!strcmp(c, "sleep")) pet_event(p, PET_SLEEP);
    else if (!strcmp(c, "wake")) pet_event(p, PET_WAKE);
    ui_unlock();
    if (!strcmp(c, "urge") || !strcmp(c, "pool") || !strcmp(c, "set")) tune();
}

static void debug_task(void *arg)
{
    (void)arg;
    usb_serial_jtag_driver_config_t cfg = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    cfg.rx_buffer_size = 512;
    ble_lines = xQueueCreateWithCaps(8, 160, MALLOC_CAP_SPIRAM); /* internal RAM is the scarce one */
    hist_load();
    if (usb_serial_jtag_driver_install(&cfg) != ESP_OK) vTaskDelete(NULL);
    static char line[200];
    int len = 0;
    int64_t next_status = 0, next_hist = 0;
    for (;;) {
        uint8_t buf[64];
        int r = usb_serial_jtag_read_bytes(buf, sizeof buf, pdMS_TO_TICKS(50));
        for (int i = 0; i < r; i++) {
            if (buf[i] == '\n') line[len] = 0, command(line), len = 0;
            else if (len < (int)sizeof line - 1) line[len++] = (char)buf[i];
        }
        char bl[160];
        while (xQueueReceive(ble_lines, bl, 0)) command(bl);
        int64_t now = esp_timer_get_time();
        if (now >= next_status) next_status = now + 1000000, status();
        if (now >= next_hist && time(NULL) > 1735689600) { next_hist = now + 60000000; ui_lock(), hist_add(), ui_unlock(); }
        if (demo_s && now >= demo_next) { /* the show: one mood after another */
            demo_next = now + (int64_t)demo_s * 1000000;
            ui_lock(), pip_set_mood(demo_list[demo_k++ % demo_n]), ui_unlock();
        }
    }
}

void debug_start(void) { xTaskCreate(debug_task, "debug", 4096, NULL, 3, NULL); }
