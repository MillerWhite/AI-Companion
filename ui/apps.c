/* The menu's apps. Each one builds its screen in open() and cleans its timers in close().
   Colours, fonts, the clock face and the meters come from the current theme (theme.h). */
#include "doodle.h"
#include "maze.h"
#include "mic.h"
#include "phone.h"
#include "pip.h"
#include "profile.h"
#include "shell.h"
#include "theme.h"
#include "voice.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define SCR 466

static lv_obj_t *dot(lv_obj_t *parent, int x, int y, int size, uint32_t color)
{
    return th_box(parent, x - size / 2, y - size / 2, size, size, LV_RADIUS_CIRCLE, color);
}

static void recolor(lv_obj_t *o, uint32_t color) { lv_obj_set_style_bg_color(o, lv_color_hex(color), 0); }

static struct tm local_now(void)
{
    time_t now = (time_t)shell_local_time();
    struct tm tm;
    gmtime_r(&now, &tm);
    return tm;
}

static const char *const WD[] = {"вс", "пн", "вт", "ср", "чт", "пт", "сб"};
static const char *const MON[] = {"янв", "фев", "мар", "апр", "мая", "июн", "июл", "авг", "сен", "окт", "ноя", "дек"};

/* ================= clock: one face per theme ================= */

#define MCOLS 25 /* HH:MM = 5+1+5+1+1+1+5+1+5 */

/* a word for how he is, for the clock and the care screen */
static const char *pip_word(const pip_info_t *in)
{
    if (in->asleep) return "спит";
    if (in->food < 0.25f) return "голоден";
    if (in->energy < 0.25f) return "устал";
    if (in->fun < 0.25f) return "скучает";
    if (in->love < 0.25f) return "одинок";
    return in->day_mood > 0.3f ? "доволен" : in->day_mood < -0.3f ? "не в духе" : "спокоен";
}

static struct {
    lv_obj_t *cells[MCOLS * 7], *ring[60], *date, *info, *window, *hands[3], *arcs[3], *comp[2];
    lv_obj_t *grid, *rows[3]; /* the terminal face: one object for all the cells, three data rows */
    th_cell_t tc[MCOLS * 7 + 60];
    int nd;
    lv_point_precise_t pts[3][2], ticks[60][2];
    lv_timer_t *timer;
} ck;

static void matrix_set(int hh, int mm)
{
    int digits[4] = {hh / 10, hh % 10, mm / 10, mm % 10}, starts[4] = {0, 6, 14, 20};
    for (int r = 0; r < 7; r++)
        for (int c = 0; c < MCOLS; c++) {
            bool on = (c == 12 && (r == 2 || r == 4)); /* the colon */
            for (int d = 0; d < 4; d++)
                if (c >= starts[d] && c < starts[d] + 5) on = th_glyph[digits[d]][r][c - starts[d]] == '1';
            recolor(ck.cells[r * MCOLS + c], on ? th->text : th->faint);
        }
}

static void clock_tick(lv_timer_t *t)
{
    (void)t;
    struct tm tm = local_now();
    pip_info_t in;
    pip_get_info(&in);
    if (th->clock == CLOCK_DOTS) matrix_set(tm.tm_hour, tm.tm_min);
    if (th->clock == CLOCK_DOTS) {
        for (int i = 0; i < 60; i++) recolor(ck.ring[i], i == tm.tm_sec ? th->accent : i < tm.tm_sec ? th->dim : th->faint);
        char d[32];
        snprintf(d, sizeof d, "%s %d %s", WD[tm.tm_wday], tm.tm_mday, MON[tm.tm_mon]);
        lv_label_set_text(ck.date, th_label(d));
    } else if (th->clock == CLOCK_BLOCKS) {
        char t[48];
        snprintf(t, sizeof t, "%02d:%02d", tm.tm_hour, tm.tm_min);
        th_blocks_show(ck.grid, ck.tc, t, th->text);
        for (int i = 0; i < 60; i++) th_cell_set(ck.grid, &ck.tc[ck.nd + i], i < tm.tm_sec ? th->text : th->off); /* a cell a second */
        snprintf(t, sizeof t, "%s  %d %s %d", WD[tm.tm_wday], tm.tm_mday, MON[tm.tm_mon], tm.tm_year + 1900);
        lv_label_set_text(ck.date, th_label(t));
        snprintf(t, sizeof t, "%d", in.steps);
        th_leader(ck.rows[0], "шаги", t, 202);
        th_leader(ck.rows[1], "заряд", shell_battery_text(), 202);
        th_leader(ck.rows[2], "пип", th_label(pip_word(&in)), 202);
    } else if (th->clock == CLOCK_DIAL) {
        float ang[3] = {(tm.tm_hour % 12 + tm.tm_min / 60.0f) * 30, (tm.tm_min + tm.tm_sec / 60.0f) * 6, tm.tm_sec * 6.0f};
        const float len[3] = {112, 168, 186}, tail[3] = {16, 20, 36};
        for (int i = 0; i < 3; i++) {
            float a = ang[i] * 3.14159f / 180, sx = sinf(a), cy = -cosf(a);
            ck.pts[i][0] = (lv_point_precise_t){SCR / 2 - sx * tail[i], SCR / 2 - cy * tail[i]};
            ck.pts[i][1] = (lv_point_precise_t){SCR / 2 + sx * len[i], SCR / 2 + cy * len[i]};
            lv_line_set_points(ck.hands[i], ck.pts[i], 2);
        }
        lv_label_set_text_fmt(ck.window, "%02d:%02d:%02d", tm.tm_hour, tm.tm_min, tm.tm_sec);
    } else { /* CLOCK_BIG */
        lv_label_set_text_fmt(ck.info, "%02d:%02d", tm.tm_hour, tm.tm_min);
        char d[24];
        snprintf(d, sizeof d, "%s %d", WD[tm.tm_wday], tm.tm_mday);
        lv_label_set_text(ck.date, d);
        float v[3] = {in.food, in.fun, in.love};
        for (int i = 0; i < 3; i++) lv_arc_set_value(ck.arcs[i], (int32_t)(v[i] * 100));
        lv_label_set_text_fmt(ck.comp[0], "%d", in.steps);
        lv_label_set_text_fmt(ck.comp[1], "%d%%", (int)(in.energy * 100));
    }
}

static void matrix_make(lv_obj_t *scr, int y0, bool round)
{
    const int pitch = 15, size = round ? 11 : 12, x0 = (SCR - MCOLS * pitch) / 2;
    for (int r = 0; r < 7; r++)
        for (int c = 0; c < MCOLS; c++)
            ck.cells[r * MCOLS + c] = th_box(scr, x0 + c * pitch, y0 + r * pitch, size, size, round ? LV_RADIUS_CIRCLE : 0, th->faint);
}

static lv_obj_t *ring_arc(lv_obj_t *scr, int size, int width, uint32_t color, int y)
{
    lv_obj_t *a = lv_arc_create(scr);
    lv_obj_set_size(a, size, size);
    lv_obj_align(a, LV_ALIGN_TOP_MID, 0, y - size / 2);
    lv_arc_set_range(a, 0, 100);
    lv_arc_set_bg_angles(a, 0, 360);
    lv_arc_set_rotation(a, 270);
    lv_obj_set_style_arc_width(a, width, LV_PART_MAIN);
    lv_obj_set_style_arc_width(a, width, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(a, lv_color_hex(color), LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(a, lv_color_hex(color), LV_PART_MAIN);
    lv_obj_set_style_arc_opa(a, LV_OPA_30, LV_PART_MAIN);
    lv_obj_set_style_opa(a, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_remove_flag(a, LV_OBJ_FLAG_CLICKABLE);
    return a;
}

static void clock_open(lv_obj_t *scr)
{
    if (th->clock == CLOCK_DOTS) {
        for (int i = 0; i < 60; i++) {
            float a = i * 6 * 3.14159f / 180;
            ck.ring[i] = dot(scr, (int)(SCR / 2 + sinf(a) * 212), (int)(SCR / 2 - cosf(a) * 212), i % 5 ? 6 : 10, th->faint);
        }
        matrix_make(scr, 168, true);
        ck.date = th_text(scr, th->small, th->dim, "");
        lv_obj_align(ck.date, LV_ALIGN_CENTER, 0, 92);
    } else if (th->clock == CLOCK_BLOCKS) {
        th_header(scr, "пип-01 // время", 52);
        ck.date = th_text_at(scr, th->tiny, th->dim, "", -1, 125);
        ck.nd = th_blocks(ck.tc, 97, 163, "00:00", 11, 9);
        th_ring(ck.tc + ck.nd, 60, 214, 8);
        ck.grid = th_cells(scr, ck.tc, ck.nd + 60);
        for (int i = 0; i < 3; i++) ck.rows[i] = th_text_at(scr, th->tiny, th->text, "", 132, 263 + i * 26);
        th_footer(scr, TH_DOWN " назад", 363);
    } else if (th->clock == CLOCK_DIAL) {
        for (int i = 0; i < 60; i++) {
            float a = i * 6 * 3.14159f / 180, r0 = i % 5 ? 202 : 186;
            ck.ticks[i][0] = (lv_point_precise_t){SCR / 2 + sinf(a) * r0, SCR / 2 - cosf(a) * r0};
            ck.ticks[i][1] = (lv_point_precise_t){SCR / 2 + sinf(a) * 214, SCR / 2 - cosf(a) * 214};
            lv_obj_t *l = lv_line_create(scr);
            lv_line_set_points(l, ck.ticks[i], 2);
            lv_obj_set_style_line_width(l, i % 5 ? 2 : 4, 0);
            lv_obj_set_style_line_color(l, lv_color_hex(i % 15 == 0 ? th->accent : i % 5 ? th->faint : th->text), 0);
        }
        static const char *const nums[4] = {"12", "3", "6", "9"};
        for (int i = 0; i < 4; i++) {
            float a = i * 90 * 3.14159f / 180;
            lv_obj_t *n = th_text(scr, th->body, th->text, nums[i]);
            lv_obj_align(n, LV_ALIGN_CENTER, (int)(sinf(a) * 156), (int)(-cosf(a) * 156));
        }
        ck.window = th_text(scr, th->small, th->dim, "");
        lv_obj_set_style_border_width(ck.window, 2, 0);
        lv_obj_set_style_border_color(ck.window, lv_color_hex(th->faint), 0);
        lv_obj_set_style_radius(ck.window, th->radius, 0);
        lv_obj_set_style_pad_hor(ck.window, 12, 0);
        lv_obj_set_style_pad_ver(ck.window, 4, 0);
        lv_obj_align(ck.window, LV_ALIGN_CENTER, 0, 78);
        const int width[3] = {10, 6, 3};
        const uint32_t color[3] = {th->text, th->text, th->accent};
        for (int i = 0; i < 3; i++) {
            ck.hands[i] = lv_line_create(scr);
            lv_obj_set_style_line_width(ck.hands[i], width[i], 0);
            lv_obj_set_style_line_rounded(ck.hands[i], true, 0);
            lv_obj_set_style_line_color(ck.hands[i], lv_color_hex(color[i]), 0);
        }
        dot(scr, SCR / 2, SCR / 2, 18, th->accent);
    } else { /* CLOCK_BIG: date in red, the time, rings for food/fun/love, two little complications */
        ck.date = th_text(scr, th->body, 0xFF4245, "");
        lv_obj_align(ck.date, LV_ALIGN_TOP_MID, 0, 62);
        ck.info = th_text(scr, th->digits, th->text, "");
        lv_obj_align(ck.info, LV_ALIGN_TOP_MID, 0, 100);
        ck.arcs[0] = ring_arc(scr, 108, 11, 0xFF375F, 330);
        ck.arcs[1] = ring_arc(scr, 80, 11, 0x30D158, 330);
        ck.arcs[2] = ring_arc(scr, 52, 11, 0x3CD3FE, 330);
        for (int i = 0; i < 2; i++) {
            lv_obj_t *c = th_box(scr, i ? 312 : 74, 292, 80, 80, LV_RADIUS_CIRCLE, th->panel);
            ck.comp[i] = th_text(c, th->small, i ? 0x30D158 : 0x0091FF, "");
            lv_obj_center(ck.comp[i]);
        }
    }
    clock_tick(NULL);
    ck.timer = lv_timer_create(clock_tick, 250, NULL);
}

static void clock_close(void) { lv_timer_delete(ck.timer); }

/* ================= timer: drag the arc to set minutes, it keeps running when you leave ================= */

static struct {
    int minutes;
    bool running, open;
    uint32_t end_tick, left_ms; /* left_ms when paused */
    lv_obj_t *arc, *time, *start;
    lv_timer_t *ui;
    /* the terminal face: a ring of 60 one-minute cells with a knob, block digits, two buttons */
    lv_obj_t *grid, *head, *sub, *go, *pause, *foot;
    th_cell_t tc[70 + 147 + 60 + 2]; /* "MM" while setting, "MM:SS" while counting, the ring, the knob */
} tm_ = {.minutes = 5};
#define TM_MM 0
#define TM_MMSS 70
#define TM_RING (70 + 147)
#define TM_KNOB (70 + 147 + 60)

static uint32_t timer_left(void)
{
    if (!tm_.running) return tm_.left_ms;
    int32_t left = (int32_t)(tm_.end_tick - lv_tick_get());
    return left > 0 ? (uint32_t)left : 0;
}

static void timer_ui_term(void)
{
    uint32_t left = timer_left();
    bool fresh = !tm_.running && left == (uint32_t)tm_.minutes * 60000;
    char t[32];
    lv_label_set_text(tm_.head, th_label(fresh ? "пип-01 // таймер" : tm_.running ? "таймер // идёт" : "таймер // пауза"));
    if (fresh) {
        snprintf(t, sizeof t, "%02d", tm_.minutes);
        th_blocks_hide(tm_.grid, tm_.tc + TM_MMSS, 147);
        th_blocks_show(tm_.grid, tm_.tc + TM_MM, t, th->text);
        lv_label_set_text(tm_.sub, th_label("минут"));
    } else {
        snprintf(t, sizeof t, "%02u:%02u", (unsigned)(left / 60000), (unsigned)(left / 1000 % 60));
        th_blocks_hide(tm_.grid, tm_.tc + TM_MM, 70);
        th_blocks_show(tm_.grid, tm_.tc + TM_MMSS, t, tm_.running ? th->text : th->dim);
        snprintf(t, sizeof t, "из %02d:00", tm_.minutes);
        lv_label_set_text(tm_.sub, th_label(t));
    }
    /* a cell a minute: set, or still to go; the running minute blinks */
    int lit = fresh ? tm_.minutes : (int)((left + 59999) / 60000);
    bool blink = tm_.running && lv_tick_get() / 500 % 2;
    for (int i = 0; i < 60; i++)
        th_cell_set(tm_.grid, &tm_.tc[TM_RING + i], i < lit - 1 || (i == lit - 1 && !blink) ? th->text : i == lit - 1 ? th->dim : th->off);
    th_cell_t *k = &tm_.tc[TM_KNOB];
    float a = (tm_.minutes - 0.5f) * 6.2831853f / 60 - 1.5707963f;
    int kx = (int)lroundf(SCR / 2 + cosf(a) * 196), ky = (int)lroundf(SCR / 2 + sinf(a) * 196);
    if (k[0].x != kx - 11 || k[0].y != ky - 11 || (k[0].color == TH_NONE) == fresh) {
        k[0] = (th_cell_t){(int16_t)(kx - 11), (int16_t)(ky - 11), 22, 22, fresh ? th->text : TH_NONE};
        k[1] = (th_cell_t){(int16_t)(kx - 4), (int16_t)(ky - 4), 8, 8, fresh ? th->bg : TH_NONE};
        lv_obj_invalidate(tm_.grid);
    }
    lv_obj_set_flag(tm_.go, LV_OBJ_FLAG_HIDDEN, tm_.running);
    lv_obj_set_flag(tm_.pause, LV_OBJ_FLAG_HIDDEN, !tm_.running);
    lv_label_set_text(tm_.foot, th_label(fresh ? TH_DOWN " назад" : TH_DOWN " сброс"));
}

static void timer_ui(lv_timer_t *t)
{
    (void)t;
    if (!tm_.open) return;
    if (TH_TERMINAL) { timer_ui_term(); return; }
    uint32_t left = timer_left();
    bool fresh = !tm_.running && left == (uint32_t)tm_.minutes * 60000;
    lv_label_set_text_fmt(tm_.time, "%02u:%02u", (unsigned)(left / 60000), (unsigned)((left / 1000) % 60));
    lv_label_set_text(lv_obj_get_child(tm_.start, 0), tm_.running ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);
    if (!fresh) lv_arc_set_value(tm_.arc, (int32_t)((left + 59999) / 60000));
    lv_obj_set_state(tm_.arc, LV_STATE_DISABLED, !fresh); /* the knob only sets a fresh timer */
}

static void timer_watch(lv_timer_t *t) /* runs always: rings even when the app is closed */
{
    (void)t;
    if (tm_.running && timer_left() == 0) {
        tm_.running = false;
        tm_.left_ms = (uint32_t)tm_.minutes * 60000;
        shell_home();
        pip_react(PIP_SURPRISED, 4, "time's up");
    }
}

static void timer_set(lv_event_t *e)
{
    (void)e;
    tm_.minutes = lv_arc_get_value(tm_.arc);
    tm_.left_ms = (uint32_t)tm_.minutes * 60000;
    timer_ui(NULL);
}

static void timer_start(lv_event_t *e)
{
    (void)e;
    if (tm_.running) tm_.left_ms = timer_left(), tm_.running = false;
    else tm_.end_tick = lv_tick_get() + tm_.left_ms, tm_.running = true;
    timer_ui(NULL);
}

static void timer_reset(lv_event_t *e)
{
    (void)e;
    tm_.running = false;
    tm_.left_ms = (uint32_t)tm_.minutes * 60000;
    if (tm_.arc) lv_arc_set_value(tm_.arc, tm_.minutes);
    timer_ui(NULL);
}

/* top: start and pause; bottom: reset a used timer, or leave a fresh one */
static bool timer_button(int id)
{
    bool fresh = !tm_.running && timer_left() == (uint32_t)tm_.minutes * 60000;
    if (id == PIP_BTN_PWR) timer_start(NULL);
    else if (!fresh) timer_reset(NULL);
    else return false;
    return true;
}

static void timer_drag(lv_event_t *e) /* the terminal ring: drag round it to set the minutes */
{
    (void)e;
    if (tm_.running || timer_left() != (uint32_t)tm_.minutes * 60000) return;
    lv_point_t p;
    lv_indev_get_point(lv_indev_active(), &p);
    float dx = p.x - SCR / 2, dy = p.y - SCR / 2;
    if (hypotf(dx, dy) < 140) return; /* the middle is for the button */
    float a = atan2f(dx, -dy) * 57.2958f;
    int m = (int)ceilf((a < 0 ? a + 360 : a) / 6);
    tm_.minutes = m < 1 ? 1 : m > 60 ? 60 : m;
    tm_.left_ms = (uint32_t)tm_.minutes * 60000;
    timer_ui(NULL);
}

static void timer_open_term(lv_obj_t *scr)
{
    tm_.head = th_header(scr, "", 92);
    th_blocks(tm_.tc + TM_MM, 174, 163, "00", 11, 9);
    th_blocks(tm_.tc + TM_MMSS, 97, 163, "00:00", 11, 9);
    th_ring(tm_.tc + TM_RING, 60, 196, 9);
    tm_.tc[TM_KNOB] = tm_.tc[TM_KNOB + 1] = (th_cell_t){0, 0, 1, 1, TH_NONE};
    tm_.grid = th_cells(scr, tm_.tc, TM_KNOB + 2);
    tm_.sub = th_text_at(scr, th->tiny, th->dim, "", -1, 251);
    lv_obj_set_style_text_letter_space(tm_.sub, 2, 0);
    tm_.go = th_button_at(scr, 163, 286, 140, TH_UP " старт", true, timer_start);
    tm_.pause = th_button_at(scr, 163, 286, 140, TH_UP " пауза", false, timer_start);
    tm_.foot = th_footer(scr, "", 347);
    lv_obj_add_flag(scr, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(scr, timer_drag, LV_EVENT_PRESSING, NULL);
}

static void timer_open(lv_obj_t *scr)
{
    if (!tm_.running && !tm_.left_ms) tm_.left_ms = (uint32_t)tm_.minutes * 60000;
    tm_.open = true;
    if (TH_TERMINAL) {
        timer_open_term(scr);
        timer_ui(NULL);
        tm_.ui = lv_timer_create(timer_ui, 100, NULL);
        return;
    }
    tm_.arc = lv_arc_create(scr);
    lv_obj_set_size(tm_.arc, 400, 400);
    lv_obj_center(tm_.arc);
    lv_arc_set_range(tm_.arc, 1, 60);
    lv_arc_set_value(tm_.arc, tm_.minutes);
    lv_arc_set_bg_angles(tm_.arc, 135, 45);
    lv_obj_set_style_arc_width(tm_.arc, 14, LV_PART_MAIN);
    lv_obj_set_style_arc_width(tm_.arc, 14, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(tm_.arc, th->radius != 0, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(tm_.arc, th->radius != 0, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(tm_.arc, lv_color_hex(th->faint), LV_PART_MAIN);
    lv_obj_set_style_arc_color(tm_.arc, lv_color_hex(th->accent), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(tm_.arc, lv_color_hex(th->text), LV_PART_KNOB);
    lv_obj_set_style_radius(tm_.arc, th->radius, LV_PART_KNOB);
    lv_obj_set_style_pad_all(tm_.arc, 6, LV_PART_KNOB);
    lv_obj_add_event_cb(tm_.arc, timer_set, LV_EVENT_VALUE_CHANGED, NULL);
    tm_.time = th_text(scr, th->digits, th->text, "");
    lv_obj_align(tm_.time, LV_ALIGN_CENTER, 0, -30);
    tm_.start = th_button(scr, LV_SYMBOL_PLAY, true, timer_start);
    lv_obj_set_size(tm_.start, 84, 64);
    lv_obj_align(tm_.start, LV_ALIGN_CENTER, -50, 92);
    lv_obj_t *reset = th_button(scr, LV_SYMBOL_REFRESH, false, timer_reset);
    lv_obj_set_size(reset, 84, 64);
    lv_obj_align(reset, LV_ALIGN_CENTER, 50, 92);
    timer_ui(NULL);
    tm_.ui = lv_timer_create(timer_ui, 200, NULL);
}

static void timer_close(void)
{
    lv_timer_delete(tm_.ui);
    tm_.arc = NULL;
    tm_.open = false;
}

/* ================= torch: the whole AMOLED as a lamp; tap for colour, drag up/down for brightness ================= */

static struct {
    lv_obj_t *scr, *hint;
    int color;
    float level;
    int32_t drag_y;
} tc = {.level = 1};

static void torch_paint(void)
{
    static const uint32_t colors[] = {0xFFFFFF, 0xFFD9A0, 0xFF2A1A};
    uint32_t c = colors[tc.color];
    lv_obj_set_style_bg_image_src(tc.scr, NULL, 0);
    lv_obj_set_style_bg_color(tc.scr, lv_color_make((uint8_t)((c >> 16 & 255) * tc.level), (uint8_t)((c >> 8 & 255) * tc.level),
                                                    (uint8_t)((c & 255) * tc.level)), 0);
}

static void torch_input(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_point_t p;
    lv_indev_get_point(lv_indev_active(), &p);
    if (code == LV_EVENT_PRESSED) tc.drag_y = p.y;
    else if (code == LV_EVENT_PRESSING) {
        tc.level = fmaxf(0.15f, fminf(1, tc.level - (p.y - tc.drag_y) / 300.0f));
        tc.drag_y = p.y;
        torch_paint();
    } else if (code == LV_EVENT_SHORT_CLICKED) {
        tc.color = (tc.color + 1) % 3;
        torch_paint();
    }
}

static void torch_open(lv_obj_t *scr)
{
    tc.scr = scr;
    torch_paint();
    tc.hint = th_text(scr, th->small, 0x777777, "тап — цвет\nвверх/вниз — яркость");
    lv_obj_center(tc.hint);
    lv_obj_fade_out(tc.hint, 600, 1800);
    lv_obj_add_flag(scr, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(scr, torch_input, LV_EVENT_ALL, NULL);
}

/* ================= quick QR: your links to show people; swipe left/right ================= */
/* Built here with qrcodegen at HIGH error correction (30%) instead of lv_qrcode (15%),
   so the brand mark in the middle can cover modules and the code still scans. */
#include "src/libs/qrcode/qrcodegen.h"

#define QR_COUNT (profile_links() ? profile_links() : 1) /* the links set in the panel (profile.h) */
#define QR_PX 240
#define QR_TERM 252 /* the terminal's code fills its ink plate, modules in the background colour */
static struct {
    lv_obj_t *pg[2], *img[2], *service[2], *name[2]; /* two pages: the one up and the one sliding in */
    lv_obj_t *dots[8], *head, *grid;
    th_cell_t pages[8];
    lv_draw_buf_t *buf[8];   /* every code drawn once: the Telegram plane takes longer than a frame */
    lv_timer_t *warm;
    int i, size, dir, theme, cur, rev; /* rev: the profile the codes were drawn from */
    uint8_t done;             /* which bufs hold their code for this theme */
} qr;

static void qr_render(int i)
{
    if (qr.done >> i & 1) return;
    qr.done |= (uint8_t)(1 << i);
    const char *text = profile_link(i)->url;
    int brand = profile_brand(text);
    static uint8_t code[qrcodegen_BUFFER_LEN_FOR_VERSION(10)], tmp[qrcodegen_BUFFER_LEN_FOR_VERSION(10)];
    uint32_t *px = (uint32_t *)qr.buf[i]->data, size = qr.size;
    uint32_t light = TH_TERMINAL ? 0xFF000000 | th->text : 0xFFFFFFFF, dark = TH_TERMINAL ? 0xFF000000 | th->bg : 0xFF000000;
    for (uint32_t i = 0; i < size * size; i++) px[i] = light;
    if (!qrcodegen_encodeText(text, tmp, code, qrcodegen_Ecc_HIGH, 1, 10, qrcodegen_Mask_AUTO, true)) return;
    /* whole pixels per module and a quiet zone: four modules on white, the plate's 10 px edge in the terminal */
    int n = qrcodegen_getSize(code), m = TH_TERMINAL ? (int)(size - 20) / n : (int)size / (n + 4), off = ((int)size - m * n) / 2;
    for (int y = 0; y < n; y++)
        for (int x = 0; x < n; x++)
            if (qrcodegen_getModule(code, x, y))
                for (int dy = 0; dy < m; dy++)
                    for (int dx = 0; dx < m; dx++) px[(off + y * m + dy) * size + off + x * m + dx] = dark;
    int logo = (int)(m * n * 0.26f); /* about a quarter of the code: well inside what HIGH can lose */
    if (brand && TH_TERMINAL) doodle_brand_ink(brand, px, (int)size, ((int)size - logo) / 2, ((int)size - logo) / 2, logo, dark, light);
    else if (brand) doodle_brand(brand, px, (int)size, ((int)size - logo) / 2, ((int)size - logo) / 2, logo);
}

static void qr_warm(lv_timer_t *t) /* the others, one a tick, while nobody is swiping yet */
{
    for (int i = 0; i < QR_COUNT && i < 8; i++)
        if (!(qr.done >> i & 1)) { qr_render(i); return; }
    lv_timer_delete(t), qr.warm = NULL;
}

static void qr_fill(int p, int i) /* page p shows code i */
{
    qr_render(i);
    lv_image_set_src(qr.img[p], qr.buf[i]);
    if (TH_TERMINAL) {
        char t[96];
        snprintf(t, sizeof t, "%s #%06X // %s#", profile_link(i)->service, (unsigned)th->dim, profile_link(i)->handle);
        lv_label_set_text(qr.service[p], th_label(t));
        return;
    }
    lv_label_set_text(qr.service[p], th_label(profile_link(i)->service));
    lv_label_set_text(qr.name[p], profile_link(i)->handle); /* handles stay as they are */
}

static void qr_marks(void) /* where we are: the header and the page cells, or the dots */
{
    if (TH_TERMINAL) {
        char t[48];
        snprintf(t, sizeof t, "пип-01 // qr %d/%d", qr.i + 1, QR_COUNT);
        lv_label_set_text(qr.head, th_label(t));
        for (int k = 0; k < QR_COUNT && k < 8; k++) th_cell_set(qr.grid, &qr.pages[k], k == qr.i ? th->text : th->off);
    } else
        for (int k = 0; k < QR_COUNT && k < 8; k++) lv_obj_set_style_bg_opa(qr.dots[k], k == qr.i ? LV_OPA_COVER : LV_OPA_30, 0);
}

/* a carousel: the code goes with the finger and the next one comes in right behind it */
static void qr_slide(void *v, int32_t t)
{
    (void)v;
    int x = qr.dir * SCR * t / 1000;
    lv_obj_set_style_translate_x(qr.pg[qr.cur], -x, 0);
    lv_obj_set_style_translate_x(qr.pg[!qr.cur], qr.dir * SCR - x, 0);
}

static void qr_landed(lv_anim_t *a)
{
    (void)a;
    qr.cur = !qr.cur;
    lv_obj_add_flag(qr.pg[!qr.cur], LV_OBJ_FLAG_HIDDEN);
}

static void qr_gesture(lv_event_t *e)
{
    (void)e;
    lv_dir_t d = lv_indev_get_gesture_dir(lv_indev_active());
    if (d != LV_DIR_LEFT && d != LV_DIR_RIGHT) return;
    if (lv_anim_delete(&qr, qr_slide)) qr_slide(NULL, 1000), qr_landed(NULL); /* a swipe mid-slide: land the last one first */
    qr.dir = d == LV_DIR_LEFT ? 1 : -1;
    qr.i = (qr.i + QR_COUNT + qr.dir) % QR_COUNT;
    qr_fill(!qr.cur, qr.i);
    lv_obj_remove_flag(qr.pg[!qr.cur], LV_OBJ_FLAG_HIDDEN);
    qr_marks();
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, &qr);
    lv_anim_set_exec_cb(&a, qr_slide);
    lv_anim_set_values(&a, 0, 1000);
    lv_anim_set_duration(&a, 320);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_set_completed_cb(&a, qr_landed);
    lv_anim_start(&a);
}

static void qr_close(void)
{
    lv_anim_delete(&qr, qr_slide);
    if (qr.warm) lv_timer_delete(qr.warm), qr.warm = NULL;
}

static void qr_open(lv_obj_t *scr)
{
    int size = TH_TERMINAL ? QR_TERM : QR_PX;
    if (qr.theme != theme_index() + 1) qr.done = 0, qr.theme = theme_index() + 1; /* other colours */
    if (qr.rev != profile_rev) qr.done = 0, qr.rev = profile_rev, qr.i = 0;  /* other links */
    for (int k = 0; k < QR_COUNT && k < 8; k++) {
        if (qr.buf[k] && qr.size != size) lv_draw_buf_destroy(qr.buf[k]), qr.buf[k] = NULL, qr.done = 0;
        if (!qr.buf[k]) qr.buf[k] = lv_draw_buf_create(size, size, LV_COLOR_FORMAT_ARGB8888, 0);
    }
    qr.size = size;
    if (!qr.warm) qr.warm = lv_timer_create(qr_warm, 60, NULL);
    lv_obj_add_event_cb(scr, qr_gesture, LV_EVENT_GESTURE, NULL);
    for (int p = 0; p < 2; p++) {
        lv_obj_t *pg = qr.pg[p] = th_box(scr, 0, 0, SCR, SCR, 0, 0);
        lv_obj_set_style_bg_opa(pg, LV_OPA_TRANSP, 0);
        if (TH_TERMINAL) {
            qr.img[p] = lv_image_create(pg);
            lv_obj_set_pos(qr.img[p], 107, 78);
            qr.service[p] = th_text_at(pg, th->tiny_bold, th->text, "", -1, 345);
            lv_label_set_recolor(qr.service[p], true);
            continue;
        }
        lv_obj_t *card = th_box(pg, (SCR - 270) / 2, (SCR - 270) / 2 + 18, 270, 270, th->radius ? 22 : 0, 0xFFFFFF);
        qr.img[p] = lv_image_create(card);
        lv_obj_center(qr.img[p]);
        qr.service[p] = th_text(pg, th->small, th->dim, "");
        lv_obj_align(qr.service[p], LV_ALIGN_TOP_MID, 0, 30);
        qr.name[p] = th_text(pg, th->body, th->text, "");
        lv_obj_set_style_text_letter_space(qr.name[p], 0, 0);
        lv_obj_align(qr.name[p], LV_ALIGN_TOP_MID, 0, 56);
    }
    if (TH_TERMINAL) {
        qr.head = th_header(scr, "", 40);
        for (int k = 0; k < QR_COUNT && k < 8; k++) qr.pages[k] = (th_cell_t){(int16_t)(SCR / 2 - (QR_COUNT * 11 - 2) / 2 + k * 11), 376, 9, 9, th->off};
        qr.grid = th_cells(scr, qr.pages, QR_COUNT < 8 ? QR_COUNT : 8);
        th_footer(scr, TH_LEFT " " TH_RIGHT " свайп   " TH_DOWN " назад", 399);
    } else
        for (int k = 0; k < QR_COUNT && k < 8; k++)
            qr.dots[k] = dot(scr, SCR / 2 + (int)((k - (QR_COUNT - 1) / 2.0f) * 18), SCR - 54, 8, th->text);
    qr.cur = 0;
    lv_obj_add_flag(qr.pg[1], LV_OBJ_FLAG_HIDDEN);
    qr_fill(0, qr.i);
    qr_marks();
}

/* ================= Pip: needs, feed and play; the meters follow the theme ================= */

typedef struct { lv_obj_t *cells[16], *bar, *val; int n; uint32_t color; } meter_t;

static void meter_make(meter_t *m, lv_obj_t *scr, int x, int y, uint32_t color)
{
    memset(m, 0, sizeof *m);
    m->color = color;
    if (th->meter == METER_DOTS || th->meter == METER_SEGMENTS) {
        bool dots = th->meter == METER_DOTS;
        m->n = dots ? 10 : 16;
        for (int i = 0; i < m->n; i++)
            m->cells[i] = dots ? th_box(scr, x + i * 16, y + 4, 11, 11, LV_RADIUS_CIRCLE, th->faint) : th_box(scr, x + i * 10, y, 7, 18, 1, th->faint);
    } else {
        m->bar = lv_bar_create(scr);
        bool blocks = th->meter == METER_BLOCKS;
        lv_obj_set_size(m->bar, blocks ? 120 : 112, blocks ? 16 : 8);
        lv_obj_set_pos(m->bar, x, y + (blocks ? 2 : 6));
        lv_obj_set_style_radius(m->bar, blocks ? 0 : 4, LV_PART_MAIN);
        lv_obj_set_style_radius(m->bar, blocks ? 0 : 4, LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(m->bar, lv_color_hex(th->faint), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(m->bar, LV_OPA_COVER, LV_PART_MAIN);
    }
    if (th->meter != METER_DOTS) {
        m->val = th_text(scr, th->small, th->text, "");
        lv_obj_set_pos(m->val, x + (m->bar ? 132 : 164), y - 2);
    }
}

static void meter_set(meter_t *m, float v)
{
    bool low = v < 0.25f;
    uint32_t c = low ? (th == &themes[TH_APPLE] ? 0xFF4245 : th->accent) : m->color;
    if (m->bar) {
        lv_bar_set_value(m->bar, (int32_t)(v * 100), LV_ANIM_OFF);
        lv_obj_set_style_bg_color(m->bar, lv_color_hex(c), LV_PART_INDICATOR);
    }
    for (int i = 0; i < m->n; i++) recolor(m->cells[i], i < (int)roundf(v * m->n) ? c : th->faint);
    if (m->val) lv_label_set_text_fmt(m->val, th->meter == METER_BLOCKS ? "%d %%" : "%d", (int)(v * 100));
}

static struct {
    meter_t m[5];
    lv_obj_t *state, *grid, *vals[5];
    th_cell_t tc[5 * 12 + 20];
    int shown[5], want[5], nbody, nsound;
    lv_timer_t *timer, *fill;
} care;

/* terminal meters: 12 cells each, filling a cell every 40 ms up (or down) to the value */
static void care_fill(lv_timer_t *t)
{
    (void)t;
    for (int i = 0; i < 5; i++) {
        if (care.shown[i] == care.want[i]) continue;
        care.shown[i] += care.shown[i] < care.want[i] ? 1 : -1;
        for (int k = 0; k < 12; k++) th_cell_set(care.grid, &care.tc[i * 12 + k], k < care.shown[i] ? th->text : th->off);
    }
}

static void care_sound_cells(void) /* the speaker always, its waves only when the sound is on */
{
    for (int k = 60 + care.nbody; k < 60 + care.nsound; k++) th_cell_set(care.grid, &care.tc[k], voice_volume() > 0 ? th->text : TH_NONE);
}

static void care_tick(lv_timer_t *t)
{
    (void)t;
    pip_info_t in;
    pip_get_info(&in);
    float v[5] = {in.food, in.energy, in.fun, in.love, in.walk};
    if (TH_TERMINAL) {
        for (int i = 0; i < 5; i++) {
            care.want[i] = (int)lroundf(v[i] * 12);
            lv_label_set_text_fmt(care.vals[i], "%d%%", (int)(v[i] * 100));
            lv_obj_set_style_text_color(care.vals[i], lv_color_hex(v[i] < 0.25f ? th->dim : th->text), 0);
        }
        char s[64];
        snprintf(s, sizeof s, "состояние: %s", pip_word(&in));
        lv_label_set_text(care.state, th_label(s));
        return;
    }
    for (int i = 0; i < 5; i++) meter_set(&care.m[i], v[i]);
    char s[64];
    snprintf(s, sizeof s, "%s, шагов: %d", in.asleep ? "спит" : "не спит", in.steps);
    lv_label_set_text(care.state, th_label(s));
}

static void care_sound(lv_event_t *e)
{
    lv_obj_t *b = lv_event_get_target(e);
    voice_set_volume(voice_volume() > 0 ? 0 : 0.8f);
    if (TH_TERMINAL) {
        care_sound_cells();
        if (voice_volume() > 0) voice_say(VOICE_HAPPY);
        return;
    }
    lv_label_set_text(lv_obj_get_child(b, 0), voice_volume() > 0 ? LV_SYMBOL_AUDIO : LV_SYMBOL_CLOSE);
    if (voice_volume() > 0) voice_say(VOICE_HAPPY);
}

static void care_feed(lv_event_t *e) { (void)e; pip_action(PIP_ACT_FEED); shell_home(); }
static void care_play(lv_event_t *e) { (void)e; pip_action(PIP_ACT_PLAY); shell_home(); }

static void care_open_term(lv_obj_t *scr, const char *const *names)
{
    th_header(scr, "пип-01 // забота", 40);
    care.state = th_text_at(scr, th->tiny, th->dim, "", -1, 89);
    for (int i = 0; i < 5; i++) {
        int y = 128 + i * 30;
        th_text_at(scr, th->tiny, th->dim, names[i], 86, y - 2);
        care.vals[i] = th_text_at(scr, th->tiny, th->text, "", 334, y - 2);
        lv_obj_set_width(care.vals[i], 48);
        lv_obj_set_style_text_align(care.vals[i], LV_TEXT_ALIGN_RIGHT, 0);
        for (int k = 0; k < 12; k++) care.tc[i * 12 + k] = (th_cell_t){(int16_t)(196 + k * 11), (int16_t)y, 9, 16, th->off};
        care.shown[i] = 0;
    }
    th_button_at(scr, 88, 292, 120, "кормить", true, care_feed);
    th_button_at(scr, 218, 292, 108, "играть", false, care_play);
    lv_obj_t *snd = th_button_at(scr, 336, 292, 44, "", false, care_sound);
    lv_obj_delete(lv_obj_get_child(snd, 0));
    /* a speaker in 4 px cells inside the outlined button; its waves go when the sound is off */
    static const char *const speaker[] = {"0001", "0011", "1111", "1111", "1111", "0011", "0001"},
                             *const waves[] = {"01", "00", "10", "10", "10", "00", "01"};
    care.nbody = th_pattern(care.tc + 60, speaker, 7, 341, 297, 5, 4, th->text);
    care.nsound = care.nbody + th_pattern(care.tc + 60 + care.nbody, waves, 7, 366, 297, 5, 4, th->text);
    care.grid = th_cells(scr, care.tc, 60 + care.nsound);
    care_sound_cells();
    th_footer(scr, TH_DOWN " назад", 365);
    care.fill = lv_timer_create(care_fill, 40, NULL);
}

static void care_open(lv_obj_t *scr)
{
    static const char *const names[5] = {"Еда", "Энергия", "Веселье", "Внимание", "Прогулки"};
    if (TH_TERMINAL) {
        care_open_term(scr, names);
        care_tick(NULL);
        care.timer = lv_timer_create(care_tick, 500, NULL);
        return;
    }
    /* Apple tints each need with a system colour; the other themes use one ink */
    static const uint32_t apple[5] = {0xFF9230, 0x30D158, 0xFF375F, 0x3CD3FE, 0x0091FF};
    lv_obj_t *title = th_text(scr, th->body, th == &themes[TH_APPLE] ? 0x30D158 : th->text, "Пип");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 40);
    for (int i = 0; i < 5; i++) {
        int y = 96 + i * 40;
        lv_obj_t *l = th_text(scr, th->small, th->dim, names[i]);
        lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_LEFT, 0);
        lv_obj_set_pos(l, 62, y);
        meter_make(&care.m[i], scr, 196, y + 2, th == &themes[TH_APPLE] ? apple[i] : th->good);
    }
    care.state = th_text(scr, th->small, th->dim, "");
    lv_obj_align(care.state, LV_ALIGN_TOP_MID, 0, 302);
    lv_obj_t *row = lv_obj_create(scr);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(row, 8, 0);
    lv_obj_align(row, LV_ALIGN_TOP_MID, 0, 340);
    th_button(row, "Кормить", true, care_feed);
    th_button(row, "Играть", false, care_play);
    th_button(row, voice_volume() > 0 ? LV_SYMBOL_AUDIO : LV_SYMBOL_CLOSE, false, care_sound);
    care_tick(NULL);
    care.timer = lv_timer_create(care_tick, 500, NULL);
}

static void care_close(void)
{
    lv_timer_delete(care.timer);
    if (care.fill) lv_timer_delete(care.fill), care.fill = NULL;
}

/* ================= ball: roll onto the hearts before the ring runs out; touching the rim starts over ===== */

#define ROUND_MS 20000 /* a full ring */
#define HEART_MS 2500  /* each heart buys a little time back */
static struct {
    lv_obj_t *ball, *star, *score, *ring, *over;
    lv_obj_t *grid, *rows[2]; /* terminal: rings of cells, the end-of-round card */
    th_cell_t tc[60 + 96], ball_tc[9], heart_tc[25];
    int32_t flash_ms;
    lv_timer_t *timer;
    float x, y, vx, vy, sx, sy, gx, gy, bx, by;
    bool calibrated, ended;
    int points, best;
    int32_t left_ms;
    uint32_t last;
} gm;

static void game_score(void)
{
    if (TH_TERMINAL) {
        lv_label_set_text_fmt(gm.score, "%s %04d // %s %04d", th_label("счёт"), gm.points, th_label("рекорд"), gm.best);
        lv_obj_remove_flag(gm.score, LV_OBJ_FLAG_HIDDEN);
    } else lv_label_set_text_fmt(gm.score, "%d", gm.points);
}

static void game_ring(void)
{
    if (!TH_TERMINAL) {
        lv_arc_set_value(gm.ring, gm.left_ms * 1000 / ROUND_MS);
        lv_obj_set_style_arc_color(gm.ring, lv_color_hex(gm.flash_ms > 0 ? 0xFF4245 : th->accent), LV_PART_INDICATOR);
        return;
    }
    int lit = (int)((gm.left_ms * 60 + ROUND_MS - 1) / ROUND_MS);
    bool flash = gm.flash_ms > 0, on = gm.flash_ms / 100 % 2; /* three blinks when the rim is touched */
    for (int i = 0; i < 60; i++) th_cell_set(gm.grid, &gm.tc[i], flash ? (on ? th->text : th->off) : i < lit ? th->text : th->off);
}

static void game_place_star(void)
{
    float a = (float)lv_rand(0, 6283) / 1000, r = (float)lv_rand(30, 150);
    gm.sx = SCR / 2 + cosf(a) * r, gm.sy = SCR / 2 + sinf(a) * r;
    lv_obj_set_pos(gm.star, (int32_t)gm.sx - lv_obj_get_width(gm.star) / 2, (int32_t)gm.sy - lv_obj_get_height(gm.star) / 2);
}

static void game_best(void)
{
    if (gm.points <= gm.best) return;
    if (gm.best >= 3) pip_game(PIP_GAME_RECORD); /* beat a real score: he is amazed */
    gm.best = gm.points;
}

static void game_restart(void)
{
    gm.x = gm.y = SCR / 2, gm.vx = gm.vy = 0;
    gm.points = 0, gm.left_ms = ROUND_MS, gm.ended = false;
    game_score();
    lv_obj_add_flag(gm.over, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(gm.ball, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(gm.star, LV_OBJ_FLAG_HIDDEN);
    game_place_star();
}

static void game_tick(lv_timer_t *t)
{
    (void)t;
    uint32_t el = lv_tick_elaps(gm.last);
    gm.last = lv_tick_get();
    if (gm.ended) return;
    float dt = fminf(el / 1000.0f, 0.05f);
    if ((gm.left_ms -= (int32_t)el) <= 0) { /* time's up */
        gm.ended = true, gm.left_ms = 0;
        game_best();
        char s[96];
        if (TH_TERMINAL) {
            snprintf(s, sizeof s, "%04d", gm.points);
            th_leader(gm.rows[0], "счёт", s, 202);
            snprintf(s, sizeof s, "%04d", gm.best);
            th_leader(gm.rows[1], "рекорд", s, 202);
            lv_obj_add_flag(gm.score, LV_OBJ_FLAG_HIDDEN);
            gm.flash_ms = 0;
        } else {
            snprintf(s, sizeof s, "Время!\nсчёт %d, лучший %d\nтап — заново", gm.points, gm.best);
            lv_label_set_text(gm.over, th_label(s));
        }
        lv_obj_remove_flag(gm.over, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(gm.ball, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(gm.star, LV_OBJ_FLAG_HIDDEN);
    }
    gm.flash_ms -= (int32_t)el;
    game_ring();
    gm.vx += gm.gx * 1600 * dt, gm.vy += gm.gy * 1600 * dt;
    gm.vx *= 0.985f, gm.vy *= 0.985f;
    gm.x += gm.vx * dt, gm.y += gm.vy * dt;
    float rim = TH_TERMINAL ? 196 - 2 - 16 : SCR / 2 - 18 - 22;
    if (hypotf(gm.x - SCR / 2, gm.y - SCR / 2) > rim) { /* touched the rim: the count starts over */
        game_best();
        game_restart();
        gm.flash_ms = 600;
        return;
    }
    if (hypotf(gm.x - gm.sx, gm.y - gm.sy) < 38) {
        gm.points++;
        gm.left_ms = gm.left_ms + HEART_MS > ROUND_MS ? ROUND_MS : gm.left_ms + HEART_MS;
        game_score();
        game_place_star();
    }
    lv_obj_set_pos(gm.ball, (int32_t)gm.x - lv_obj_get_width(gm.ball) / 2, (int32_t)gm.y - lv_obj_get_height(gm.ball) / 2);
}

static void game_imu(float ax, float ay, float az, float dt)
{
    (void)az;
    if (!gm.calibrated) gm.bx = ax, gm.by = ay, gm.calibrated = true;
    float k = 1 - expf(-dt / 6.0f); /* slowly follow the way the keychain is held */
    gm.bx += (ax - gm.bx) * k, gm.by += (ay - gm.by) * k;
    gm.gx = -(ax - gm.bx), gm.gy = ay - gm.by; /* downhill on the screen */
}

static void game_tap(lv_event_t *e)
{
    (void)e;
    if (gm.ended) game_restart();
}

static bool game_button(int id) /* top starts a new round once time is up */
{
    if (id != PIP_BTN_PWR || !gm.ended) return false;
    game_restart();
    return true;
}

static lv_obj_t *cell_sprite(lv_obj_t *scr, th_cell_t *c, const char *const *rows, int n, uint32_t color)
{
    lv_obj_t *o = lv_obj_create(scr);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, (int)strlen(rows[0]) * 11 - 2, n * 11 - 2);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
    th_cells(o, c, th_pattern(c, rows, n, 0, 0, 11, 9, color));
    return o;
}

static void game_open_term(lv_obj_t *scr)
{
    th_ring(gm.tc, 60, 216, 8);
    th_ring(gm.tc + 60, 96, 196, 3); /* the rim: touch it and the run starts over */
    gm.grid = th_cells(scr, gm.tc, 60 + 96);
    gm.score = th_text_at(scr, th->tiny, th->mute, "", -1, 97);
    static const char *const heart[] = {"01010", "11111", "11111", "01110", "00100"}, *const ball[] = {"111", "111", "111"};
    gm.star = cell_sprite(scr, gm.heart_tc, heart, 5, th->dim);
    gm.ball = cell_sprite(scr, gm.ball_tc, ball, 3, th->text);
    gm.over = lv_obj_create(scr); /* the end of a round: a plate, the score, again */
    lv_obj_remove_style_all(gm.over);
    lv_obj_set_size(gm.over, SCR, SCR);
    lv_obj_remove_flag(gm.over, LV_OBJ_FLAG_CLICKABLE);
    th_plate(gm.over, 83, 140, 300, 48, th->body, "время вышло", false, NULL);
    for (int i = 0; i < 2; i++) gm.rows[i] = th_text_at(gm.over, th->tiny, i ? th->dim : th->text, "", 132, 213 + i * 26);
    th_button_at(gm.over, 153, 282, 160, TH_UP " ещё раз", true, game_tap);
    th_footer(gm.over, TH_DOWN " назад", 347);
}

static void game_open(lv_obj_t *scr)
{
    int best = gm.best;
    memset(&gm, 0, sizeof gm);
    gm.best = best;
    gm.last = lv_tick_get();
    gm.ring = lv_arc_create(scr); /* the time left, running down around the rim */
    lv_obj_set_size(gm.ring, SCR - 16, SCR - 16);
    lv_obj_center(gm.ring);
    lv_arc_set_range(gm.ring, 0, 1000);
    lv_arc_set_bg_angles(gm.ring, 0, 360);
    lv_arc_set_rotation(gm.ring, 270);
    lv_obj_set_style_arc_width(gm.ring, 8, LV_PART_MAIN);
    lv_obj_set_style_arc_width(gm.ring, 8, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(gm.ring, th->radius != 0, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(gm.ring, lv_color_hex(th->faint), LV_PART_MAIN);
    lv_obj_set_style_opa(gm.ring, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_remove_flag(gm.ring, LV_OBJ_FLAG_CLICKABLE);
    if (TH_TERMINAL) lv_obj_delete(gm.ring), gm.ring = NULL;
    if (TH_TERMINAL) {
        game_open_term(scr);
        lv_obj_add_flag(scr, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(scr, game_tap, LV_EVENT_CLICKED, NULL);
        game_restart();
        gm.timer = lv_timer_create(game_tick, 16, NULL);
        return;
    }
    gm.score = th_text(scr, th->digits, th->faint, "0");
    lv_obj_center(gm.score);
    gm.star = th_text(scr, th->body, th == &themes[TH_APPLE] ? 0xFF375F : th->accent, "\xEF\x80\x84"); /* U+F004, a heart */
    lv_obj_set_size(gm.star, 44, 44);
    gm.ball = th->radius ? dot(scr, SCR / 2, SCR / 2, 44, th->text) : th_box(scr, SCR / 2 - 20, SCR / 2 - 20, 40, 40, 0, th->text);
    gm.over = th_text(scr, th->body, th->text, "");
    lv_obj_set_style_bg_color(gm.over, lv_color_hex(th->panel), 0);
    lv_obj_set_style_bg_opa(gm.over, LV_OPA_90, 0);
    lv_obj_set_style_pad_all(gm.over, 18, 0);
    lv_obj_set_style_radius(gm.over, th->radius ? 24 : 0, 0);
    lv_obj_center(gm.over);
    lv_obj_add_flag(scr, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(scr, game_tap, LV_EVENT_CLICKED, NULL);
    game_restart();
    gm.timer = lv_timer_create(game_tick, 16, NULL);
}

static void game_close(void)
{
    lv_timer_delete(gm.timer);
    if (gm.best >= 3) pip_action(PIP_ACT_PLAY); /* playing with him counts */
}

/* ================= maze: tilt the ball from the top left corner to the mark in the bottom right ================= */
/* Four sizes, each solved maze leads to a bigger one; the top button deals a new one. The walls are one A8 image in
   the theme's ink, drawn once a maze; the ball and the mark are small objects. The physics is ui/maze.c. */

#define MZ_SIDE 308                /* the maze's square, centred: its corners stay inside the round glass */
#define MZ_IMG (MZ_SIDE + 12)      /* the walls' image, with room for their thickness round the edge */
#define MZ_PUSH 1100               /* px/s^2 for a tilt of 1 g */
static const int mz_sizes[] = {6, 8, 10, 12};
static struct {
    maze_t m;
    lv_obj_t *walls, *ball, *goal, *label;
    lv_timer_t *timer;
    float bx, by, gx, gy, next_in; /* the hand's usual pose, the push now; next_in > 0: solved, the next one comes */
    bool calibrated;
    int level, solved;
    uint32_t last;
} mz;
static lv_draw_buf_t *mz_buf; /* kept between games: one allocation */

static float mz_cover(float a0, float a1, int p) { return fmaxf(0, fminf(a1, p + 1.0f) - fmaxf(a0, (float)p)); } /* of pixel p */

static void mz_fill(float x0, float y0, float x1, float y1) /* a rectangle, its edges antialiased, image px */
{
    uint8_t *px = mz_buf->data;
    int stride = (int)mz_buf->header.stride;
    for (int y = (int)fmaxf(0, floorf(y0)); y < (int)fminf(MZ_IMG, ceilf(y1)); y++) {
        float cy = mz_cover(y0, y1, y);
        for (int x = (int)fmaxf(0, floorf(x0)); x < (int)fminf(MZ_IMG, ceilf(x1)); x++) {
            int a = px[y * stride + x] + (int)(255 * cy * mz_cover(x0, x1, x) + 0.5f);
            px[y * stride + x] = (uint8_t)(a > 255 ? 255 : a);
        }
    }
}

static void mz_draw(void)
{
    static float r[MAZE_MAX * MAZE_MAX * 2 + MAZE_MAX * 2][4];
    lv_draw_buf_clear(mz_buf, NULL);
    int n = maze_rects(&mz.m, r, sizeof r / sizeof r[0]);
    const float o = (MZ_IMG - MZ_SIDE) / 2.0f; /* the maze's corner in the image */
    for (int k = 0; k < n; k++) {
        float x0 = r[k][0] + o, y0 = r[k][1] + o, x1 = r[k][2] + o, y1 = r[k][3] + o;
        if (!TH_TERMINAL) { mz_fill(x0, y0, x1, y1); continue; }
        /* the terminal: a wall is a row of square cells, crisp, on whole pixels */
        int q = (int)mz.m.t + 2, along = x1 - x0 > y1 - y0;
        float len = along ? x1 - x0 : y1 - y0;
        int cnt = (int)((len - q) / (q + 3)) + 1;
        float gap = cnt > 1 ? (len - q) / (cnt - 1) : 0;
        for (int i = 0; i < cnt; i++) {
            float a = (along ? x0 : y0) + i * gap, b = (along ? (y0 + y1) : (x0 + x1)) / 2 - q / 2.0f;
            float cx = roundf(along ? a : b), cy = roundf(along ? b : a);
            mz_fill(cx, cy, cx + q, cy + q);
        }
    }
    lv_obj_invalidate(mz.walls);
}

static void mz_place(lv_obj_t *o, float x, float y) /* centre an object on a maze point */
{
    lv_obj_set_pos(o, (int32_t)lroundf((SCR - MZ_SIDE) / 2 + x) - lv_obj_get_width(o) / 2,
                   (int32_t)lroundf((SCR - MZ_SIDE) / 2 + y) - lv_obj_get_height(o) / 2);
}

static void mz_new(void)
{
    maze_new(&mz.m, mz_sizes[mz.level], MZ_SIDE, lv_rand(1, 0x7FFFFFFF));
    mz.next_in = 0;
    mz_draw();
    int d = (int)(mz.m.r * 2), g = (int)(mz.m.c * 0.44f);
    lv_obj_set_size(mz.ball, d, d);
    lv_obj_set_size(mz.goal, g, g);
    mz_place(mz.goal, (mz.m.n - 0.5f) * mz.m.c, (mz.m.n - 0.5f) * mz.m.c);
    mz_place(mz.ball, mz.m.x, mz.m.y);
    char s[48];
    if (TH_TERMINAL) snprintf(s, sizeof s, "пип-01 // лабиринт %d/4", mz.level + 1);
    else snprintf(s, sizeof s, "уровень %d", mz.level + 1);
    lv_label_set_text(mz.label, th_label(s));
}

static void mz_tick(lv_timer_t *t)
{
    (void)t;
    float dt = fminf(lv_tick_elaps(mz.last) / 1000.0f, 0.05f); /* never more than half a cell a frame: see maze_test */
    mz.last = lv_tick_get();
    if (mz.next_in > 0) { /* solved: a moment to see it, then a bigger one */
        if ((mz.next_in -= dt) <= 0) mz.level = mz.level + 1 < 4 ? mz.level + 1 : 3, mz_new();
        return;
    }
    maze_step(&mz.m, mz.gx * MZ_PUSH, mz.gy * MZ_PUSH, dt);
    mz_place(mz.ball, mz.m.x, mz.m.y);
    if (maze_done(&mz.m)) {
        mz.solved++, mz.next_in = 0.8f;
        voice_say(VOICE_HAPPY);
    }
}

static void mz_imu(float ax, float ay, float az, float dt)
{
    (void)az;
    if (!mz.calibrated) mz.bx = ax, mz.by = ay, mz.calibrated = true;
    float k = 1 - expf(-dt / 6.0f); /* slowly follow the way the keychain is held, as the ball game does */
    mz.bx += (ax - mz.bx) * k, mz.by += (ay - mz.by) * k;
    mz.gx = -(ax - mz.bx), mz.gy = ay - mz.by; /* downhill on the screen */
}

static bool mz_button(int id) /* top: another maze of this size */
{
    if (id != PIP_BTN_PWR) return false;
    mz_new();
    return true;
}

static void mz_open(lv_obj_t *scr)
{
    int level = mz.level, solved = mz.solved;
    memset(&mz, 0, sizeof mz);
    mz.level = level, mz.solved = solved;
    if (!mz_buf) mz_buf = lv_draw_buf_create(MZ_IMG, MZ_IMG, LV_COLOR_FORMAT_A8, 0);
    LV_ASSERT_MALLOC(mz_buf);
    mz.walls = lv_image_create(scr);
    lv_image_set_src(mz.walls, mz_buf);
    lv_obj_set_pos(mz.walls, (SCR - MZ_IMG) / 2, (SCR - MZ_IMG) / 2);
    lv_obj_set_style_image_recolor(mz.walls, lv_color_hex(TH_TERMINAL ? th->dim : th->text), 0);
    lv_obj_set_style_image_recolor_opa(mz.walls, LV_OPA_COVER, 0);
    if (TH_TERMINAL) {
        mz.label = th_header(scr, "", 40);
        th_footer(scr, TH_UP " новый   " TH_DOWN " назад", 404);
        mz.goal = th_cursor(scr, 12, 12, th->text); /* a blinking block, sized per maze below */
        mz.ball = th_box(scr, 0, 0, 10, 10, 0, th->text);
    } else {
        mz.label = th_text(scr, th->small, th->dim, "");
        lv_obj_align(mz.label, LV_ALIGN_TOP_MID, 0, 34);
        mz.goal = dot(scr, 0, 0, 12, th == &themes[TH_APPLE] ? 0xFF375F : th->accent);
        mz.ball = dot(scr, 0, 0, 10, th->text);
    }
    lv_obj_remove_flag(mz.ball, LV_OBJ_FLAG_CLICKABLE);
    mz_new();
    mz.last = lv_tick_get();
    mz.timer = lv_timer_create(mz_tick, 16, NULL);
}

static void mz_close(void)
{
    lv_timer_delete(mz.timer);
    if (mz.solved) pip_action(PIP_ACT_PLAY); /* playing with him counts */
}

/* ================= stopwatch: top button starts and marks laps, bottom resets; tap pauses ================= */

#define MAX_LAPS 99
static struct {
    bool running;
    uint32_t start, acc; /* ms: when it last started, and what came before */
    uint32_t laps[MAX_LAPS];
    int n;
    lv_obj_t *time, *cs, *list;
    lv_timer_t *ui;
    lv_obj_t *grid, *lap_rows[3], *foot; /* terminal */
    th_cell_t tc[147];
    int drawn;
} sw;

static uint32_t sw_elapsed(void) { return sw.acc + (sw.running ? lv_tick_elaps(sw.start) : 0); }

static void sw_draw_term(void)
{
    uint32_t e = sw_elapsed();
    char t[32];
    snprintf(t, sizeof t, "%02u:%02u", (unsigned)(e / 60000 % 100), (unsigned)(e / 1000 % 60));
    th_blocks_show(sw.grid, sw.tc, t, sw.running || !e ? th->text : th->dim);
    lv_label_set_text_fmt(sw.cs, ".%02u", (unsigned)(e / 10 % 100));
    int state = sw.n * 4 + sw.running * 2 + (e > 0);
    if (state == sw.drawn) return;
    sw.drawn = state;
    for (int k = 0; k < 3; k++) { /* the last three laps, newest first */
        int i = sw.n - 1 - k;
        if (i < 0) { lv_label_set_text(sw.lap_rows[k], ""); continue; }
        uint32_t lap = sw.laps[i] - (i ? sw.laps[i - 1] : 0);
        char key[16];
        snprintf(key, sizeof key, "кр %02d", i + 1);
        snprintf(t, sizeof t, "%02u:%02u.%02u", (unsigned)(lap / 60000), (unsigned)(lap / 1000 % 60), (unsigned)(lap / 10 % 100));
        th_leader(sw.lap_rows[k], key, t, 234);
    }
    lv_label_set_text(sw.foot, th_label(sw.running ? TH_UP " круг   " TH_DOWN " сброс" : e ? TH_UP " дальше   " TH_DOWN " сброс"
                                                                                       : TH_UP " старт   " TH_DOWN " назад"));
}

static void sw_draw(lv_timer_t *t)
{
    (void)t;
    if (!sw.time) return;
    if (TH_TERMINAL) { sw_draw_term(); return; }
    uint32_t e = sw_elapsed();
    lv_label_set_text_fmt(sw.time, "%02u:%02u", (unsigned)(e / 60000 % 100), (unsigned)(e / 1000 % 60));
    lv_label_set_text_fmt(sw.cs, ".%02u", (unsigned)(e / 10 % 100));
    char buf[160] = "", line[48];
    for (int i = sw.n - 1; i >= 0 && i >= sw.n - 3; i--) { /* the last three laps, newest first */
        uint32_t lap = sw.laps[i] - (i ? sw.laps[i - 1] : 0);
        snprintf(line, sizeof line, "%sкруг %d   %02u:%02u.%02u", buf[0] ? "\n" : "", i + 1, (unsigned)(lap / 60000),
                 (unsigned)(lap / 1000 % 60), (unsigned)(lap / 10 % 100));
        strncat(buf, line, sizeof buf - strlen(buf) - 1);
    }
    lv_label_set_text(sw.list, th_label(buf));
    lv_obj_set_style_text_color(sw.time, lv_color_hex(sw.running || !e ? th->text : th->dim), 0);
}

static bool sw_button(int id)
{
    if (id == PIP_BTN_PWR) { /* start, then a lap mark every press */
        if (!sw.running) sw.start = lv_tick_get(), sw.running = true;
        else if (sw.n < MAX_LAPS) sw.laps[sw.n++] = sw_elapsed();
    } else { /* reset, or leave once there is nothing to reset */
        if (!sw.running && !sw_elapsed()) return false;
        sw.running = false, sw.acc = 0, sw.n = 0;
    }
    sw_draw(NULL);
    return true;
}

static void sw_tap(lv_event_t *e) /* pause and go on */
{
    (void)e;
    if (sw.running) sw.acc = sw_elapsed(), sw.running = false;
    else if (sw.acc) sw.start = lv_tick_get(), sw.running = true;
    sw_draw(NULL);
}

static void sw_open(lv_obj_t *scr)
{
    if (TH_TERMINAL) {
        th_header(scr, "пип-01 // секундомер", 40);
        th_blocks(sw.tc, 97, 97, "00:00", 11, 9);
        sw.grid = th_cells(scr, sw.tc, 147);
        sw.time = sw.grid; /* "open" */
        sw.cs = th_text_at(scr, th->body, th->dim, "", -1, 186);
        const uint32_t ink[3] = {th->text, th->dim, th->mute};
        for (int k = 0; k < 3; k++) sw.lap_rows[k] = th_text_at(scr, th->tiny, ink[k], "", 116, 237 + k * 26);
        th_text_at(scr, th->tiny, th->mute, "тап: пауза", -1, 341);
        sw.foot = th_footer(scr, "", 367);
        sw.drawn = -1;
        lv_obj_add_flag(scr, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(scr, sw_tap, LV_EVENT_SHORT_CLICKED, NULL);
        sw_draw(NULL);
        sw.ui = lv_timer_create(sw_draw, 33, NULL);
        return;
    }
    sw.time = th_text(scr, th->digits, th->text, "");
    lv_obj_align(sw.time, LV_ALIGN_CENTER, 0, -60);
    sw.cs = th_text(scr, th->body, th->dim, "");
    lv_obj_align(sw.cs, LV_ALIGN_CENTER, 0, 4);
    sw.list = th_text(scr, th->small, th->text, "");
    lv_obj_align(sw.list, LV_ALIGN_CENTER, 0, 74);
    lv_obj_add_flag(scr, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(scr, sw_tap, LV_EVENT_SHORT_CLICKED, NULL);
    sw_draw(NULL);
    sw.ui = lv_timer_create(sw_draw, 33, NULL);
}

static void sw_close(void)
{
    lv_timer_delete(sw.ui);
    sw.time = NULL;
}

/* ================= badge: the name, big; a tap or the top button changes the layout ================= */

#ifdef KEYPET_AITU /* the owner's own build: the AITU mark (not part of the public firmware) */
extern const lv_image_dsc_t aitu_mark_420, aitu_mark_118; /* drawn at these sizes from the vector: tools/aitu.py */
#endif
#define BADGE_FIRST profile.first /* set in the panel */
#define BADGE_LAST profile.last

static struct { lv_obj_t *box; int look; } bd; /* the look stays while the badge is closed */

/* the AITU rings are round about the dot at (137, 120) of the 240 px mask: that dot goes to (cx, cy) */
static void badge_mark(int size, int cx, int cy, uint32_t color)
{
#ifndef KEYPET_AITU
    /* Pip's own eyes, drawn at the size asked for from their outline: sharp at any size */
    static lv_draw_buf_t *eyes[2];
    lv_draw_buf_t **b = &eyes[size == 118 ? 0 : 1];
    if (!*b) {
        *b = lv_draw_buf_create((uint32_t)size, (uint32_t)size, LV_COLOR_FORMAT_A8, 0);
        if (!*b) return;
        doodle_icon_mask(ICON_PIP, (*b)->data, (int)(*b)->header.stride, size);
    }
    lv_obj_t *e = lv_image_create(bd.box);
    lv_image_set_src(e, *b);
    lv_obj_set_style_image_recolor(e, lv_color_hex(color), 0);
    lv_obj_set_style_image_recolor_opa(e, LV_OPA_COVER, 0);
    lv_obj_set_pos(e, cx - size / 2, cy - size / 2);
#else
    /* each size has its own mask made from the logo's vector: a scaled raster goes soft */
    const lv_image_dsc_t *src = size == 118 ? &aitu_mark_118 : &aitu_mark_420;
    int w = (int)src->header.w;
    lv_obj_t *m = lv_image_create(bd.box);
    lv_image_set_src(m, src);
    lv_obj_set_style_image_recolor(m, lv_color_hex(color), 0);
    lv_obj_set_style_image_recolor_opa(m, LV_OPA_COVER, 0);
    lv_obj_set_pos(m, cx - 137 * w / 240, cy - 120 * w / 240);
    if (w != size) lv_image_set_pivot(m, 137 * w / 240, 120 * w / 240), lv_image_set_scale(m, (uint32_t)(256 * size / w));
#endif
}

static void badge_line(const char *text, const lv_font_t *font, uint32_t color, int y, int space)
{
    lv_obj_set_style_text_letter_space(th_text_at(bd.box, font, color, text, -1, y), space, 0);
}

static void badge_build(void)
{
    lv_obj_clean(bd.box);
    const lv_font_t *big = th->big ? th->big : th->body;
    if (bd.look == 0) { /* the name alone, on a dial */
        if (TH_TERMINAL) {
            static th_cell_t ring[72];
            th_ring(ring, 72, 214, 5);
            for (int i = 0; i < 72; i += 6) ring[i].color = th->mute;
            th_cells(bd.box, ring, 72);
        } else {
            lv_obj_t *sc = lv_scale_create(bd.box);
            lv_obj_set_size(sc, 444, 444);
            lv_obj_center(sc);
            lv_scale_set_mode(sc, LV_SCALE_MODE_ROUND_INNER);
            lv_scale_set_angle_range(sc, 360);
            lv_scale_set_rotation(sc, 270);
            lv_scale_set_total_tick_count(sc, 61);
            lv_scale_set_major_tick_every(sc, 5);
            lv_scale_set_label_show(sc, false);
            lv_obj_set_style_arc_width(sc, 0, 0);
            lv_obj_set_style_length(sc, 7, LV_PART_ITEMS);
            lv_obj_set_style_line_width(sc, 2, LV_PART_ITEMS);
            lv_obj_set_style_line_color(sc, lv_color_hex(th->faint), LV_PART_ITEMS);
            lv_obj_set_style_length(sc, 14, LV_PART_INDICATOR);
            lv_obj_set_style_line_width(sc, 2, LV_PART_INDICATOR);
            lv_obj_set_style_line_color(sc, lv_color_hex(th->dim), LV_PART_INDICATOR);
        }
        badge_line(BADGE_FIRST, big, th->text, 152, 2);
        badge_line(BADGE_LAST, big, th->text, 224, 2);
        th_box(bd.box, SCR / 2 - 40, 310, 80, 4, 0, th->accent);
        if (profile.org[0]) badge_line(profile.org, th->tiny_bold, th->dim, 336, 6);
    } else if (bd.look == 1) { /* the mark small on top, the name under it */
        badge_mark(118, SCR / 2, 112, th->text);
        badge_line(BADGE_FIRST, big, th->text, 204, 1);
        badge_line(BADGE_LAST, big, th->text, 266, 1);
        th_box(bd.box, SCR / 2 - 80, 350, 160, 2, 0, th->faint);
        if (profile.org[0]) badge_line(profile.org, th->tiny_bold, th->accent, 366, 6);
    } else { /* the mark faint behind the name */
        badge_mark(420, SCR / 2, SCR / 2, th->faint);
        badge_line(BADGE_FIRST, big, th->text, 164, 2);
        badge_line(BADGE_LAST, big, th->text, 236, 2);
        th_box(bd.box, SCR / 2 - 20, 320, 40, 4, 0, th->accent);
    }
}

static void badge_next(void) { bd.look = (bd.look + 1) % 3, badge_build(); }
static void badge_tap(lv_event_t *e) { (void)e; badge_next(); }

static bool badge_button(int id)
{
    if (id != PIP_BTN_PWR) return false;
    badge_next();
    return true;
}

static void badge_open(lv_obj_t *scr)
{
    bd.box = th_box(scr, 0, 0, SCR, SCR, 0, 0);
    lv_obj_set_style_bg_opa(bd.box, LV_OPA_TRANSP, 0);
    lv_obj_add_flag(scr, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(scr, badge_tap, LV_EVENT_CLICKED, NULL);
    badge_build();
}

/* ================= noise: a sound level meter for the studio, the colour says how quiet it is ================= */

#define NZ_LO 20  /* the dial's range, dB */
#define NZ_HI 100
#define NZ_BARS 40 /* the last 20 s, a bar per half second */

static const struct { float below; uint32_t color; const char *word; } nz_bands[] = {
    {30, 0x34C759, "тишина"}, {40, 0x9BE15D, "тихо"}, {55, 0xFFD60A, "разговор"}, {70, 0xFF9F0A, "шумно"}, {999, 0xFF453A, "громко"},
};

static struct {
    lv_obj_t *arc, *num, *unit, *word, *span, *bars;
    th_cell_t cells[NZ_BARS];
    lv_timer_t *timer;
    float slow, peak, lo, hi, hist[NZ_BARS];
    int n, ticks;
} nz;

static int nz_band(float db)
{
    int i = 0;
    while (db >= nz_bands[i].below) i++;
    return i;
}

static void nz_reset(void) { nz.lo = 999, nz.hi = 0, nz.n = 0, nz.slow = 0; }

static void nz_update(lv_timer_t *t)
{
    (void)t;
    if (!mic_on()) return;
    float db = mic_db();
    nz.slow = nz.slow ? nz.slow + (db - nz.slow) * 0.1f : db; /* about a second: the number holds still enough to read */
    nz.peak = fmaxf(nz.peak, db);
    int b = nz_band(nz.slow);
    uint32_t c = nz_bands[b].color;
    lv_arc_set_value(nz.arc, (int32_t)fminf(NZ_HI, fmaxf(NZ_LO, db)));
    lv_obj_set_style_arc_color(nz.arc, lv_color_hex(nz_bands[nz_band(db)].color), LV_PART_INDICATOR);
    lv_label_set_text_fmt(nz.num, "%d", (int)lroundf(nz.slow));
    lv_obj_set_style_text_color(nz.num, lv_color_hex(c), 0);
    lv_label_set_text(nz.word, th_label(nz_bands[b].word));
    lv_obj_set_style_text_color(nz.word, lv_color_hex(c), 0);
    if (++nz.ticks % 5) return;
    /* every half second: a bar of the loudest moment in it, and the session's quietest and loudest */
    nz.lo = fminf(nz.lo, nz.slow), nz.hi = fmaxf(nz.hi, nz.peak);
    memmove(nz.hist, nz.hist + 1, sizeof nz.hist - sizeof nz.hist[0]);
    nz.hist[NZ_BARS - 1] = nz.peak, nz.peak = 0, nz.n++;
    for (int i = 0; i < NZ_BARS; i++) {
        float v = nz.hist[i];
        int h = v > 0 ? 3 + (int)(fminf(1, fmaxf(0, (v - NZ_LO) / (NZ_HI - NZ_LO))) * 44) : 3;
        nz.cells[i].y = (int16_t)(352 - h), nz.cells[i].h = (int16_t)h;
        nz.cells[i].color = v > 0 ? nz_bands[nz_band(v)].color : th_off();
    }
    lv_obj_invalidate(nz.bars);
    char s[48];
    snprintf(s, sizeof s, "мин %d   макс %d", (int)lroundf(nz.lo), (int)lroundf(nz.hi));
    lv_label_set_text(nz.span, th_label(s));
}

static void nz_tap(lv_event_t *e) { (void)e; nz_reset(), memset(nz.hist, 0, sizeof nz.hist), nz.ticks = 4, nz_update(NULL); }

static void nz_close(void)
{
    if (nz.timer) lv_timer_delete(nz.timer), nz.timer = NULL;
}

static void nz_open(lv_obj_t *scr)
{
    nz_reset();
    memset(nz.hist, 0, sizeof nz.hist);
    nz.peak = 0, nz.ticks = 0;
    /* the dial: 20..100 dB over three quarters of a turn, open at the bottom */
    lv_obj_t *a = nz.arc = lv_arc_create(scr);
    lv_obj_set_size(a, 420, 420);
    lv_obj_center(a);
    lv_obj_remove_flag(a, LV_OBJ_FLAG_CLICKABLE);
    lv_arc_set_rotation(a, 135);
    lv_arc_set_bg_angles(a, 0, 270);
    lv_arc_set_range(a, NZ_LO, NZ_HI);
    lv_arc_set_value(a, NZ_LO);
    lv_obj_set_style_bg_opa(a, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_set_style_pad_all(a, 0, LV_PART_KNOB);
    lv_obj_set_style_arc_width(a, 10, 0);
    lv_obj_set_style_arc_color(a, lv_color_hex(th->faint), 0);
    lv_obj_set_style_arc_width(a, 10, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(a, th->radius > 0, 0);
    lv_obj_set_style_arc_rounded(a, th->radius > 0, LV_PART_INDICATOR);
    nz.num = th_text(scr, th->digits, th->text, mic_on() ? "" : "--");
    lv_obj_align(nz.num, LV_ALIGN_CENTER, 0, -50);
    nz.unit = th_text(scr, th->small, th->dim, "");
    lv_label_set_text(nz.unit, "дБ"); /* the unit keeps its case */
    lv_obj_align(nz.unit, LV_ALIGN_CENTER, 0, 14);
    nz.word = th_text(scr, th->body, th->dim, mic_on() ? "" : "нет микрофона");
    lv_obj_align(nz.word, LV_ALIGN_CENTER, 0, 52);
    for (int i = 0; i < NZ_BARS; i++) nz.cells[i] = (th_cell_t){(int16_t)(SCR / 2 - NZ_BARS * 6 / 2 + i * 6), 349, 4, 3, th_off()};
    nz.bars = th_cells(scr, nz.cells, NZ_BARS);
    nz.span = th_text(scr, th->tiny, th_mute(), "");
    lv_obj_align(nz.span, LV_ALIGN_TOP_MID, 0, 364);
    lv_obj_add_flag(scr, LV_OBJ_FLAG_CLICKABLE); /* a tap starts the min/max and the bars over */
    lv_obj_add_event_cb(scr, nz_tap, LV_EVENT_SHORT_CLICKED, NULL);
    nz.timer = lv_timer_create(nz_update, 100, NULL);
    nz_update(NULL);
}

/* ================= style: pick a theme ================= */

static void style_apply(void *i) { shell_set_theme((int)(intptr_t)i); }
static void style_pick(lv_event_t *e) { lv_async_call(style_apply, lv_event_get_user_data(e)); } /* after this screen is gone */

/* the themes on offer (some are hidden for now), in order */
static struct { int focus, n, ids[TH_COUNT]; lv_obj_t *rows[TH_COUNT], *labels[TH_COUNT], *cursors[TH_COUNT]; th_cell_t swatch[TH_COUNT * 3]; } st;

static void style_list(void)
{
    st.n = 0;
    for (int i = 0; i < TH_COUNT; i++)
        if (!themes[i].hidden) st.ids[st.n++] = i;
}

static void style_show(void) /* terminal: the focus is the inverted row, [X] the theme in use */
{
    for (int k = 0; k < st.n; k++) {
        int i = st.ids[k];
        bool f = i == st.focus;
        char t[48];
        snprintf(t, sizeof t, "[%s] %s", i == theme_index() ? "X" : " ", themes[i].name);
        lv_label_set_text(st.labels[k], th_label(t));
        lv_obj_set_style_text_color(st.labels[k], lv_color_hex(f ? th->bg : th->dim), 0);
        lv_obj_set_style_bg_opa(st.rows[k], f ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        lv_obj_set_flag(st.cursors[k], LV_OBJ_FLAG_HIDDEN, !f);
    }
}

static void style_row(lv_event_t *e) /* tap moves the focus; tap the focused row to apply it */
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    if (i == st.focus) lv_async_call(style_apply, (void *)(intptr_t)i);
    else st.focus = i, style_show();
}

static bool style_button(int id)
{
    if (!TH_TERMINAL || id != PIP_BTN_PWR) return false;
    lv_async_call(style_apply, (void *)(intptr_t)st.focus);
    return true;
}

static void style_open_term(lv_obj_t *scr)
{
    th_header(scr, "пип-01 // стиль", 40);
    st.focus = theme_index();
    style_list();
    for (int k = 0; k < st.n; k++) {
        int i = st.ids[k], y = 122 + (TH_COUNT - st.n) * 25 + k * 50; /* fewer rows sit in the middle */
        lv_obj_t *r = st.rows[k] = th_box(scr, 92, y, 282, 44, 0, th->text);
        lv_obj_add_flag(r, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(r, style_row, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(r, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_left(r, 16, 0);
        lv_obj_set_style_pad_column(r, 6, 0);
        st.labels[k] = th_text(r, th->small, th->dim, "");
        st.cursors[k] = th_cursor(r, 12, 16, th->bg);
        /* the theme's own background and accent, framed */
        st.swatch[k * 3] = (th_cell_t){384, (int16_t)(y + 9), 24, 26, th->mute};
        st.swatch[k * 3 + 1] = (th_cell_t){386, (int16_t)(y + 11), 9, 22, themes[i].bg};
        st.swatch[k * 3 + 2] = (th_cell_t){397, (int16_t)(y + 11), 9, 22, themes[i].accent};
    }
    th_cells(scr, st.swatch, st.n * 3);
    th_text_at(scr, th->tiny, th->mute, "лицо пипа не меняется", -1, 331);
    th_footer(scr, TH_UP " применить   " TH_DOWN " назад", 371);
    style_show();
}

static void style_open(lv_obj_t *scr)
{
    if (TH_TERMINAL) { style_open_term(scr); return; }
    lv_obj_t *title = th_text(scr, th->body, th->text, "Стиль");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 50);
    style_list();
    for (int k = 0; k < st.n; k++) {
        int i = st.ids[k];
        lv_obj_t *b = th_button(scr, themes[i].name, i == theme_index(), style_pick);
        lv_obj_remove_event_cb(b, style_pick);
        lv_obj_add_event_cb(b, style_pick, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        lv_obj_set_width(b, 240);
        lv_obj_align(b, LV_ALIGN_TOP_MID, 0, 108 + k * 66);
    }
}

/* ================= games: a list of its own in the menu ================= */

static void games_go(void *id) { shell_open(id); }
static void games_pick(lv_event_t *e) { lv_async_call(games_go, lv_event_get_user_data(e)); } /* after this screen is gone */

static void games_open(lv_obj_t *scr)
{
    th_header(scr, "пип-01 // игры", 40);
    int n = 0;
    for (int i = app_count; i < app_total; i++) n += apps[i].group && !strcmp(apps[i].group, "games");
    int pitch = n > 5 ? 52 : 60, y = SCR / 2 - n * pitch / 2 + 8, k = 0;
    for (int i = app_count; i < app_total; i++) {
        if (!apps[i].group || strcmp(apps[i].group, "games")) continue;
        lv_obj_t *r = th_box(scr, 40, y + k * pitch, SCR - 80, pitch - 4, th->radius, th->bg);
        lv_obj_set_style_bg_opa(r, LV_OPA_TRANSP, 0);
        lv_obj_add_flag(r, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(r, games_pick, LV_EVENT_CLICKED, (void *)apps[i].id);
        lv_obj_center(th_text(r, th->body, th->text, apps[i].name));
        k++;
    }
}

/* a shake for the toys: the jolt over gravity, with a rest so one shake is one throw */
static bool jolt(float ax, float ay, float az, float dt, float *rest)
{
    float j = fabsf(sqrtf(ax * ax + ay * ay + az * az) - 1);
    if ((*rest -= dt) > 0 || j < 0.7f) return false;
    *rest = 1.0f;
    return true;
}

/* ================= endless noughts and crosses: three marks each, the fourth takes the oldest away ================= */
/* Against Pip (you are X and start, he is O) or with a friend, passing it over. The mark that goes next is shown faint.
   No draws: someone wins sooner or later. */

typedef struct { uint8_t b[9]; int8_t q[2][3]; uint8_t n[2]; } xo_t;

static struct {
    xo_t st;
    lv_obj_t *x[9][2], *o[9], *status, *chips[2];
    lv_timer_t *think;
    int turn, won, line, mode, wins[2][2]; /* mode 0: with Pip, 1: with a friend; wins per mode */
} xo;

static const uint8_t xo_lines[8][3] = {{0, 1, 2}, {3, 4, 5}, {6, 7, 8}, {0, 3, 6}, {1, 4, 7}, {2, 5, 8}, {0, 4, 8}, {2, 4, 6}};

static void xo_place(xo_t *s, int p, int c)
{
    if (s->n[p] == 3) { /* the oldest goes */
        s->b[s->q[p][0]] = 0;
        s->q[p][0] = s->q[p][1], s->q[p][1] = s->q[p][2], s->n[p] = 2;
    }
    s->b[c] = (uint8_t)(p + 1), s->q[p][s->n[p]++] = (int8_t)c;
}

static int xo_line(const xo_t *s, int p) /* the winning line, or -1 */
{
    for (int i = 0; i < 8; i++)
        if (s->b[xo_lines[i][0]] == p + 1 && s->b[xo_lines[i][1]] == p + 1 && s->b[xo_lines[i][2]] == p + 1) return i;
    return -1;
}

static int xo_search(const xo_t *s, int p, int depth) /* the best Pip (1) can force, looking depth moves ahead */
{
    int best = p ? -100 : 100;
    for (int c = 0; c < 9; c++) {
        if (s->b[c]) continue;
        xo_t t = *s;
        xo_place(&t, p, c);
        int v = xo_line(&t, p) >= 0 ? (p ? 50 + depth : -50 - depth) : depth ? xo_search(&t, !p, depth - 1) : 0;
        best = p ? (v > best ? v : best) : (v < best ? v : best);
    }
    return best;
}

static int xo_pick(void)
{
    int moves[9], n = 0, best = -1000, pick = -1;
    for (int c = 0; c < 9; c++) if (!xo.st.b[c]) moves[n++] = c;
    for (int i = n - 1; i > 0; i--) { int j = (int)lv_rand(0, (uint32_t)i), t = moves[i]; moves[i] = moves[j], moves[j] = t; }
    for (int i = 0; i < n; i++) {
        xo_t t = xo.st;
        xo_place(&t, 1, moves[i]);
        if (xo_line(&t, 1) >= 0) return moves[i]; /* a win is never missed */
        int v = xo_search(&t, 0, 4);
        if (v > best) best = v, pick = moves[i];
    }
    if (lv_rand(0, 99) < 20) pick = moves[lv_rand(0, (uint32_t)n - 1)]; /* he is a pet, not a computer: sometimes he slips */
    return pick;
}

static void xo_draw(void)
{
    for (int c = 0; c < 9; c++) {
        int who = xo.st.b[c];
        /* the oldest mark of whoever moves next fades: it is the one that goes */
        bool going = who && xo.won < 0 && xo.st.n[who - 1] == 3 && xo.st.q[who - 1][0] == c && who - 1 == xo.turn;
        bool lit = xo.won >= 0 && xo.line >= 0 && (xo_lines[xo.line][0] == c || xo_lines[xo.line][1] == c || xo_lines[xo.line][2] == c);
        lv_opa_t opa = going ? LV_OPA_30 : xo.won >= 0 && !lit ? LV_OPA_40 : LV_OPA_COVER;
        for (int k = 0; k < 2; k++) lv_obj_set_flag(xo.x[c][k], LV_OBJ_FLAG_HIDDEN, who != 1), lv_obj_set_style_opa(xo.x[c][k], opa, 0);
        lv_obj_set_flag(xo.o[c], LV_OBJ_FLAG_HIDDEN, who != 2), lv_obj_set_style_opa(xo.o[c], opa, 0);
    }
    static const char *const say[2][4] = {{"ты выиграл", "пип выиграл", "твой ход", "пип думает"},
                                          {"крестики выиграли", "нолики выиграли", "ход крестиков", "ход ноликов"}};
    char s[64];
    snprintf(s, sizeof s, "%s   %d:%d", say[xo.mode][xo.won >= 0 ? xo.won : 2 + xo.turn], xo.wins[xo.mode][0], xo.wins[xo.mode][1]);
    lv_label_set_text(xo.status, th_label(s));
    for (int i = 0; i < 2; i++) {
        bool on = i == xo.mode;
        lv_obj_set_style_bg_opa(xo.chips[i], on ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(xo.chips[i], on ? 0 : 2, 0);
        lv_obj_set_style_text_color(lv_obj_get_child(xo.chips[i], 0), lv_color_hex(on ? th->bg : th->dim), 0);
    }
}

static void xo_end(int p)
{
    xo.won = p, xo.line = xo_line(&xo.st, p), xo.wins[xo.mode][p]++;
    if (xo.mode) { voice_say(VOICE_HAPPY); return; }
    pip_game(p ? PIP_GAME_WON : PIP_GAME_LOST); /* smug for a while, or pouting: either way it was playing with you */
}

static void xo_pip(lv_timer_t *t)
{
    (void)t;
    xo.think = NULL;
    xo_place(&xo.st, 1, xo_pick());
    voice_say(VOICE_TICK);
    if (xo_line(&xo.st, 1) >= 0) xo_end(1);
    else xo.turn = 0;
    xo_draw();
}

static void xo_new(void)
{
    memset(&xo.st, 0, sizeof xo.st);
    xo.turn = 0, xo.won = -1, xo.line = -1;
    xo_draw();
}

static void xo_tap(lv_event_t *e)
{
    (void)e;
    if (xo.won >= 0) { xo_new(); return; }
    if (xo.turn && !xo.mode) return; /* Pip's move */
    lv_point_t p;
    lv_indev_get_point(lv_indev_active(), &p);
    int cx = (p.x - (SCR / 2 - 144)) / 96, cy = (p.y - (SCR / 2 - 144)) / 96;
    if (p.x < SCR / 2 - 144 || p.y < SCR / 2 - 144 || cx > 2 || cy > 2) return;
    int c = cy * 3 + cx;
    if (xo.st.b[c]) return;
    int me = xo.mode ? xo.turn : 0;
    xo_place(&xo.st, me, c);
    voice_say(VOICE_TICK);
    if (xo_line(&xo.st, me) >= 0) xo_end(me);
    else if (xo.mode) xo.turn = !xo.turn; /* hand it over */
    else xo.turn = 1, xo.think = lv_timer_create(xo_pip, 550, NULL), lv_timer_set_repeat_count(xo.think, 1);
    xo_draw();
}

static void xo_close(void)
{
    if (xo.think) lv_timer_delete(xo.think), xo.think = NULL;
}

static void xo_mode(lv_event_t *e)
{
    xo_close();
    xo.mode = (int)(intptr_t)lv_event_get_user_data(e);
    xo_new();
}

static void xo_open(lv_obj_t *scr)
{
    int x0 = SCR / 2 - 144, y0 = SCR / 2 - 144;
    uint32_t o_ink = TH_TERMINAL ? th->dim : th->accent;
    for (int i = 1; i < 3; i++) { /* the grid */
        th_box(scr, x0 + i * 96 - 2, y0 + 10, 4, 288 - 20, 2, th->faint);
        th_box(scr, x0 + 10, y0 + i * 96 - 2, 288 - 20, 4, 2, th->faint);
    }
    static lv_point_precise_t diag[2][2] = {{{0, 0}, {48, 48}}, {{48, 0}, {0, 48}}};
    for (int c = 0; c < 9; c++) {
        int cx = x0 + c % 3 * 96 + 48, cy = y0 + c / 3 * 96 + 48;
        for (int k = 0; k < 2; k++) {
            lv_obj_t *l = xo.x[c][k] = lv_line_create(scr);
            lv_line_set_points(l, diag[k], 2);
            lv_obj_set_style_line_width(l, 8, 0);
            lv_obj_set_style_line_rounded(l, th->radius > 0, 0);
            lv_obj_set_style_line_color(l, lv_color_hex(th->text), 0);
            lv_obj_set_pos(l, cx - 24, cy - 24);
        }
        lv_obj_t *o = xo.o[c] = th_box(scr, cx - 28, cy - 28, 56, 56, th->radius ? LV_RADIUS_CIRCLE : 0, th->bg);
        lv_obj_set_style_bg_opa(o, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(o, 8, 0);
        lv_obj_set_style_border_color(o, lv_color_hex(o_ink), 0);
    }
    xo.status = th_text_at(scr, th->small, th->dim, "", -1, 44);
    /* who you play with: Pip or a friend */
    lv_obj_t *row = lv_obj_create(scr);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(row, 10, 0);
    lv_obj_align(row, LV_ALIGN_TOP_MID, 0, 392);
    static const char *const names[2] = {"с пипом", "с другом"};
    for (int i = 0; i < 2; i++) {
        lv_obj_t *c = xo.chips[i] = lv_obj_create(row);
        lv_obj_remove_style_all(c);
        lv_obj_set_size(c, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_set_style_pad_hor(c, 12, 0);
        lv_obj_set_style_pad_ver(c, 6, 0);
        lv_obj_set_style_radius(c, th->radius, 0);
        lv_obj_set_style_bg_color(c, lv_color_hex(TH_TERMINAL ? th->text : th->accent), 0);
        lv_obj_set_style_border_color(c, lv_color_hex(th->faint), 0);
        lv_obj_add_flag(c, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(c, xo_mode, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        th_text(c, th->tiny_bold, th->dim, names[i]);
    }
    lv_obj_add_flag(scr, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(scr, xo_tap, LV_EVENT_CLICKED, NULL);
    xo_new();
}

/* ================= the magic 8 ball: shake it or tap, a triangle floats up with the answer ================= */

static const char *const eight_says[] = {
    "Бесспорно", "Предрешено", "Никаких сомнений", "Определённо да", "Можешь быть уверен в этом",
    "Мне кажется — да", "Вероятнее всего", "Хорошие перспективы", "Знаки говорят — да", "Да",
    "Пока не ясно, попробуй снова", "Спроси позже", "Лучше не рассказывать", "Сейчас нельзя предсказать", "Сконцентрируйся и спроси опять",
    "Даже не думай", "Мой ответ — нет", "По моим данным — нет", "Перспективы не очень хорошие", "Весьма сомнительно",
};

static struct {
    lv_obj_t *ball, *eight, *tri, *says;
    lv_draw_buf_t *tri_buf;
    float rest;
    bool asking;
} e8;

static void e8_shake_exec(void *o, int32_t v) /* the ball rocks, less and less */
{
    lv_obj_set_style_translate_x(o, (int32_t)(sinf(v * 0.05f) * (1000 - v) / 60), 0);
}

static void e8_float_exec(void *o, int32_t v) /* up out of the dark liquid */
{
    lv_obj_set_style_opa(o, (lv_opa_t)(v * 255 / 1000), 0);
    lv_obj_set_style_translate_y(o, (1000 - v) / 50, 0);
}

static void e8_shown(lv_anim_t *a) { (void)a; e8.asking = false; }

static void e8_reveal(lv_anim_t *a)
{
    (void)a;
    lv_obj_add_flag(e8.eight, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(e8.says, th_label(eight_says[lv_rand(0, sizeof eight_says / sizeof eight_says[0] - 1)]));
    lv_obj_remove_flag(e8.tri, LV_OBJ_FLAG_HIDDEN);
    lv_anim_t b;
    lv_anim_init(&b);
    lv_anim_set_var(&b, e8.tri);
    lv_anim_set_exec_cb(&b, e8_float_exec);
    lv_anim_set_values(&b, 0, 1000);
    lv_anim_set_duration(&b, 1400);
    lv_anim_set_path_cb(&b, lv_anim_path_ease_out);
    lv_anim_set_completed_cb(&b, e8_shown);
    lv_anim_start(&b);
}

static void e8_ask(void)
{
    if (e8.asking) return;
    e8.asking = true;
    lv_obj_add_flag(e8.tri, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(e8.eight, LV_OBJ_FLAG_HIDDEN);
    voice_say(VOICE_HM);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, e8.eight); /* only the white disc rocks: moving the whole window redraws all of it */
    lv_anim_set_exec_cb(&a, e8_shake_exec);
    lv_anim_set_values(&a, 0, 1000);
    lv_anim_set_duration(&a, 700);
    lv_anim_set_completed_cb(&a, e8_reveal);
    lv_anim_start(&a);
}

static void e8_tap(lv_event_t *e) { (void)e; e8_ask(); }
static bool e8_button(int id) { if (id != PIP_BTN_PWR) return false; e8_ask(); return true; }
static void e8_imu(float ax, float ay, float az, float dt) { if (jolt(ax, ay, az, dt, &e8.rest)) e8_ask(); }

static void e8_close(void)
{
    lv_anim_delete(e8.eight, e8_shake_exec);
    lv_anim_delete(e8.tri, e8_float_exec);
    e8.asking = false;
}

static float edge(float px, float py, float x1, float y1, float x2, float y2) /* signed distance, inside positive */
{
    return ((x2 - x1) * (py - y1) - (y2 - y1) * (px - x1)) / hypotf(x2 - x1, y2 - y1);
}

static void e8_open(lv_obj_t *scr)
{
    e8.rest = 0.5f, e8.asking = false;
    /* the window of the ball: a dark disc; the triangle of the die inside, point down, drawn once */
    lv_obj_t *b = e8.ball = th_box(scr, SCR / 2 - 190, SCR / 2 - 190, 380, 380, LV_RADIUS_CIRCLE, 0x0A0A1E);
    lv_obj_set_style_border_width(b, 3, 0);
    lv_obj_set_style_border_color(b, lv_color_hex(0x1E1E3C), 0);
    lv_obj_t *disc = e8.eight = th_box(b, 190 - 80, 190 - 80, 160, 160, LV_RADIUS_CIRCLE, 0xF2F2F2);
    lv_obj_t *n8 = th_text(disc, th->digits, 0x0A0A1E, "8");
    lv_obj_center(n8);
    const int tw = 300, tht = 250;
    if (!e8.tri_buf) {
        e8.tri_buf = lv_draw_buf_create(tw, tht, LV_COLOR_FORMAT_ARGB8888, 0);
        uint32_t *px = (uint32_t *)e8.tri_buf->data;
        for (int y = 0; y < tht; y++)
            for (int x = 0; x < tw; x++) { /* inside all three edges, antialiased by the nearest one */
                float d = fminf(edge(x + 0.5f, y + 0.5f, 2, 2, tw - 2, 2), fminf(edge(x + 0.5f, y + 0.5f, tw - 2, 2, tw / 2.0f, tht - 2),
                                                                             edge(x + 0.5f, y + 0.5f, tw / 2.0f, tht - 2, 2, 2)));
                float a = d > 1 ? 1 : d < 0 ? 0 : d;
                px[(size_t)y * (e8.tri_buf->header.stride / 4) + x] = (uint32_t)(a * 255) << 24 | 0x2B3BBF;
            }
    }
    e8.tri = lv_image_create(b);
    lv_image_set_src(e8.tri, e8.tri_buf);
    lv_obj_align(e8.tri, LV_ALIGN_CENTER, 0, 12);
    e8.says = th_text(e8.tri, th->small, 0xFFFFFF, "");
    lv_obj_set_width(e8.says, 190);
    lv_label_set_long_mode(e8.says, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_line_space(e8.says, 2, 0);
    lv_obj_align(e8.says, LV_ALIGN_TOP_MID, 0, 26);
    lv_obj_add_flag(e8.tri, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(scr, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(scr, e8_tap, LV_EVENT_CLICKED, NULL);
}

/* ================= heads or tails: tossed up, turning over, lands on one side ================= */

static struct {
    lv_obj_t *coin, *heads, *tails, *eyes[2], *word, *tally;
    int face, turns, count[2];
    float rest;
    bool flying;
} cn;

static void cn_show(int face)
{
    lv_obj_set_flag(cn.heads, LV_OBJ_FLAG_HIDDEN, face != 0);
    lv_obj_set_flag(cn.tails, LV_OBJ_FLAG_HIDDEN, face != 1);
}

static void cn_exec(void *o, int32_t v)
{
    (void)o;
    float t = v / 1000.0f, th_ = t * cn.turns * 3.14159f;
    /* up and down in an arc while it turns over: seen edge-on it is a thin line, past that the other side.
       Squashed by its height, not a transform: a transform draws the coin into a layer every frame (~50 ms on the board) */
    float k = fmaxf(0.06f, fabsf(cosf(th_)));
    int h = (int)(200 * k);
    lv_obj_set_height(cn.coin, h);
    lv_obj_set_y(cn.coin, SCR / 2 - 20 - h / 2 - (int32_t)(120 * 4 * t * (1 - t)));
    for (int i = 0; i < 2; i++) lv_obj_set_height(cn.eyes[i], (int32_t)fmaxf(2, 46 * k)), lv_obj_set_y(cn.eyes[i], (int32_t)(70 * k));
    lv_obj_set_flag(cn.tails, LV_OBJ_FLAG_HIDDEN, true);
    int face = (cn.face + (int)((th_ + 1.5708f) / 3.14159f)) & 1;
    lv_obj_set_flag(cn.heads, LV_OBJ_FLAG_HIDDEN, face != 0);
    lv_obj_set_flag(cn.tails, LV_OBJ_FLAG_HIDDEN, face != 1 || k < 0.6f); /* the one only once it reads */
    if (face == 1) lv_obj_center(cn.tails);
}

static void cn_landed(lv_anim_t *a)
{
    (void)a;
    cn.face = (cn.face + cn.turns) & 1, cn.flying = false;
    cn_exec(NULL, 1000);
    cn_show(cn.face);
    cn.count[cn.face]++;
    lv_label_set_text(cn.word, th_label(cn.face ? "решка" : "орёл"));
    char s[48];
    snprintf(s, sizeof s, "орёл %d   решка %d", cn.count[0], cn.count[1]);
    lv_label_set_text(cn.tally, th_label(s));
    voice_say(VOICE_TICK);
}

static void cn_toss(void)
{
    if (cn.flying) return;
    cn.flying = true, cn.turns = (int)lv_rand(7, 12);
    lv_label_set_text(cn.word, "");
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, &cn);
    lv_anim_set_exec_cb(&a, cn_exec);
    lv_anim_set_values(&a, 0, 1000);
    lv_anim_set_duration(&a, 1300);
    lv_anim_set_completed_cb(&a, cn_landed);
    lv_anim_start(&a);
}

static void cn_tap(lv_event_t *e) { (void)e; cn_toss(); }
static bool cn_button(int id) { if (id != PIP_BTN_PWR) return false; cn_toss(); return true; }
static void cn_imu(float ax, float ay, float az, float dt) { if (jolt(ax, ay, az, dt, &cn.rest)) cn_toss(); }
static void cn_close(void) { lv_anim_delete(&cn, cn_exec), cn.flying = false; }

static void cn_open(lv_obj_t *scr)
{
    cn.rest = 0.5f, cn.flying = false;
    lv_obj_t *c = cn.coin = th_box(scr, SCR / 2 - 100, SCR / 2 - 120, 200, 200, LV_RADIUS_CIRCLE, 0xD9A441);
    lv_obj_set_style_border_width(c, 10, 0);
    lv_obj_set_style_border_color(c, lv_color_hex(0xB07F2A), 0);
    /* heads: Pip's own face; tails: a one */
    cn.heads = th_box(c, 0, 0, 200, 200, 0, 0);
    lv_obj_set_style_bg_opa(cn.heads, LV_OPA_TRANSP, 0);
    for (int i = 0; i < 2; i++) cn.eyes[i] = th_box(cn.heads, 56 + i * 56, 70, 32, 46, LV_RADIUS_CIRCLE, 0x7A5517);
    cn.tails = th_text(c, th->digits, 0x7A5517, "1");
    lv_obj_center(cn.tails);
    cn_show(cn.face);
    cn.word = th_text(scr, th->body, th->text, "");
    lv_obj_align(cn.word, LV_ALIGN_TOP_MID, 0, 330);
    cn.tally = th_text(scr, th->tiny, th->dim, "");
    lv_obj_align(cn.tally, LV_ALIGN_TOP_MID, 0, 374);
    lv_obj_add_flag(scr, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(scr, cn_tap, LV_EVENT_CLICKED, NULL);
}

/* ================= a die in a cup: a shake drops it in, it knocks about and stops, then rises to show its face ================= */
/* Looking down into a cup: x right, y down, d the depth below the rim (into the screen); faces toward you have normals
   with z < 0. Nearer is bigger: scale = (F + D_REST) / (F + d). The cube is drawn by hand into a small ARGB image
   (three faces at most, flat-shaded, pips by distance), a few ms a frame. */

#define DIE_H 40.0f      /* half the edge at rest on the floor, px */
#define DIE_IMG 248
#define CUP_F 300.0f     /* perspective */
#define CUP_FLOOR 260.0f /* the floor's depth */
#define D_REST (CUP_FLOOR - DIE_H)
#define D_SHOW 12.0f     /* where it rises to, just under the rim: big, to be read */
#define CUP_R 165.0f     /* the wall */
enum { DC_SHOW, DC_FALL, DC_REST, DC_RISE };

static struct {
    lv_obj_t *img, *shadow;
    lv_draw_buf_t *buf;
    lv_timer_t *timer;
    float x, y, d, vx, vy, vd, w[3], R[3][3], rest, t, from[3];
    int phase, ext, face;
    bool dirty;
} dc;

/* local axes: +z 1, -z 6, +x 2, -x 5, +y 3, -y 4 (opposite faces add up to 7) */
static const int8_t dc_axes[6][3] = {{0, 0, 1}, {0, 0, -1}, {1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}};
static const uint8_t dc_value[6] = {1, 6, 2, 5, 3, 4};
/* pips on the 3x3 spots: bit (row * 3 + col), rows and columns -1..1 */
static const uint16_t dc_pips[7] = {0, 0x010, 0x101, 0x111, 0x145, 0x155, 0x16D};

static void dc_rotate(float ax, float ay, float az, float ang) /* R = rot(axis, ang) * R, axis normalised */
{
    float c = cosf(ang), s = sinf(ang), t = 1 - c;
    float M[3][3] = {{t * ax * ax + c, t * ax * ay - s * az, t * ax * az + s * ay},
                     {t * ax * ay + s * az, t * ay * ay + c, t * ay * az - s * ax},
                     {t * ax * az - s * ay, t * ay * az + s * ax, t * az * az + c}};
    float N[3][3];
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++) N[i][j] = M[i][0] * dc.R[0][j] + M[i][1] * dc.R[1][j] + M[i][2] * dc.R[2][j];
    /* keep it a rotation: Gram-Schmidt on the columns */
    for (int j = 0; j < 3; j++) {
        for (int k = 0; k < j; k++) {
            float d = N[0][j] * N[0][k] + N[1][j] * N[1][k] + N[2][j] * N[2][k];
            for (int i = 0; i < 3; i++) N[i][j] -= d * N[i][k];
        }
        float l = sqrtf(N[0][j] * N[0][j] + N[1][j] * N[1][j] + N[2][j] * N[2][j]);
        for (int i = 0; i < 3; i++) dc.R[i][j] = N[i][j] / l;
    }
    dc.dirty = true;
}

static void dc_world(int f, float out[3]) /* face f's outward normal in the world */
{
    for (int i = 0; i < 3; i++) out[i] = dc.R[i][0] * dc_axes[f][0] + dc.R[i][1] * dc_axes[f][1] + dc.R[i][2] * dc_axes[f][2];
}

static void dc_draw(float H) /* H: half the edge on screen, px */
{
    uint32_t *px = (uint32_t *)dc.buf->data;
    int stride = (int)dc.buf->header.stride / 4;
    int c0 = DIE_IMG / 2 - dc.ext - 2, c1 = DIE_IMG / 2 + dc.ext + 2; /* only where the last one was */
    c0 = c0 < 0 ? 0 : c0, c1 = c1 > DIE_IMG ? DIE_IMG : c1;
    for (int y = c0; y < c1; y++) memset(px + y * stride + c0, 0, (size_t)(c1 - c0) * 4);
    dc.ext = (int)(H * 1.75f) + 2;
    uint32_t body = th->text, pip = th->bg;
    for (int f = 0; f < 6; f++) {
        float n[3];
        dc_world(f, n);
        if (n[2] > -0.06f) continue; /* facing away, or edge-on */
        /* the face's in-plane axes: the two local axes other than its own */
        int own = dc_axes[f][0] ? 0 : dc_axes[f][1] ? 1 : 2, a = (own + 1) % 3, b = (own + 2) % 3;
        float eu[2] = {dc.R[0][a] * H, dc.R[1][a] * H}, ev[2] = {dc.R[0][b] * H, dc.R[1][b] * H};
        float cx = DIE_IMG / 2.0f + n[0] * H, cy = DIE_IMG / 2.0f + n[1] * H;
        float det = eu[0] * ev[1] - eu[1] * ev[0];
        if (fabsf(det) < 1) continue;
        float ad = fabsf(det), hu = ad / hypotf(ev[0], ev[1]), hv = ad / hypotf(eu[0], eu[1]), scale = sqrtf(ad);
        /* flat shading: a light up and to the left, in front */
        float lit = 0.62f + 0.38f * fmaxf(0, -0.4f * n[0] - 0.5f * n[1] - 0.77f * n[2]);
        uint32_t r = (uint32_t)(((body >> 16) & 255) * lit), g = (uint32_t)(((body >> 8) & 255) * lit), bl = (uint32_t)((body & 255) * lit);
        float ext = fabsf(eu[0]) + fabsf(ev[0]), eyt = fabsf(eu[1]) + fabsf(ev[1]);
        int x0 = (int)fmaxf(0, cx - ext - 1), x1 = (int)fminf(DIE_IMG - 1, cx + ext + 1);
        int y0 = (int)fmaxf(0, cy - eyt - 1), y1 = (int)fminf(DIE_IMG - 1, cy + eyt + 1);
        uint16_t spots = dc_pips[dc_value[f]];
        float r_in = fmaxf(0, 0.19f - 0.5f / scale), r_out = 0.19f + 0.5f / scale, r_in2 = r_in * r_in, r_out2 = r_out * r_out;
        for (int y = y0; y <= y1; y++) {
            float dy = y + 0.5f - cy, dx = x0 + 0.5f - cx;
            float u = (dx * ev[1] - dy * ev[0]) / det, v = (dy * eu[0] - dx * eu[1]) / det;
            float du = ev[1] / det, dv = -eu[1] / det;
            for (int x = x0; x <= x1; x++, u += du, v += dv) {
                float edge = fminf((1 - fabsf(u)) * hu, (1 - fabsf(v)) * hv) + 0.5f;
                if (edge <= 0) continue;
                float cov = edge > 1 ? 1 : edge;
                /* the nearest of the nine spots, and whether this face has a pip there (no sqrt or round per pixel:
                   the ESP32 has neither in hardware) */
                int cu = u > 0.25f ? 2 : u < -0.25f ? 0 : 1, cv = v > 0.25f ? 2 : v < -0.25f ? 0 : 1;
                float k = 0; /* how much of the pip covers the pixel */
                if (spots >> (cv * 3 + cu) & 1) {
                    float pu = u - (cu - 1) * 0.5f, pv = v - (cv - 1) * 0.5f, dd = pu * pu + pv * pv;
                    if (dd < r_in2) k = 1;
                    else if (dd < r_out2) k = (0.19f - sqrtf(dd)) * scale + 0.5f;
                }
                float rim = fabsf(u) > 0.93f || fabsf(v) > 0.93f ? 0.86f : 1; /* the edges a touch darker */
                uint32_t cr = (uint32_t)((r * rim) * (1 - k) + ((pip >> 16) & 255) * k);
                uint32_t cg = (uint32_t)((g * rim) * (1 - k) + ((pip >> 8) & 255) * k);
                uint32_t cb = (uint32_t)((bl * rim) * (1 - k) + (pip & 255) * k);
                uint32_t *p = &px[y * stride + x], old = *p, oa = old >> 24;
                uint32_t na = (uint32_t)(cov * 255);
                if (oa && na < 255) { /* the seam with a face drawn before: blend over it */
                    float t = cov;
                    cr = (uint32_t)(cr * t + ((old >> 16) & 255) * (1 - t));
                    cg = (uint32_t)(cg * t + ((old >> 8) & 255) * (1 - t));
                    cb = (uint32_t)(cb * t + (old & 255) * (1 - t));
                    na = oa > na ? oa : na;
                }
                *p = na << 24 | cr << 16 | cg << 8 | cb;
            }
        }
    }
    lv_obj_invalidate(dc.img);
    dc.dirty = false;
}

static float dc_square(float k) /* turn the face most toward you a share k of the way to facing you square; the angle left */
{
    int best = 0;
    float bz = 1, n[3];
    for (int f = 0; f < 6; f++) { dc_world(f, n); if (n[2] < bz) bz = n[2], best = f; }
    dc_world(best, n);
    dc.face = dc_value[best];
    float ax = -n[1], ay = n[0], s = hypotf(ax, ay); /* n x (0,0,-1) */
    float ang = atan2f(s, -n[2]);
    if (s > 1e-4f) dc_rotate(ax / s, ay / s, 0, ang * k);
    return ang;
}

static void dc_straighten(float k) /* turn it about the view so its edges run square to the screen */
{
    for (int j = 0; j < 3; j++) {
        if (fabsf(dc.R[2][j]) > 0.5f) continue; /* an axis lying in the screen's plane */
        float th_ = atan2f(dc.R[1][j], dc.R[0][j]), q = 1.5707963f;
        dc_rotate(0, 0, 1, -(th_ - roundf(th_ / q) * q) * k);
        return;
    }
}

static void dc_throw(float dx, float dy)
{
    float l = hypotf(dx, dy), a = l > 0.3f ? atan2f(dy, dx) : lv_rand(0, 6283) / 1000.0f, sp = 500 + lv_rand(0, 500);
    dc.vx += cosf(a) * sp, dc.vy += sinf(a) * sp;
    if (dc.phase != DC_FALL) dc.vd = 250;          /* out of the hand, down into the cup */
    else if (dc.d > D_REST - 4) dc.vd = -(float)(450 + lv_rand(0, 350)); /* on the floor: the cup jumps it up */
    for (int i = 0; i < 3; i++) dc.w[i] += (lv_rand(0, 2000) / 1000.0f - 1) * 16;
    dc.phase = DC_FALL;
}

static void dc_imu(float ax, float ay, float az, float dt)
{
    /* only a shake throws it: tilting does nothing */
    float j = fabsf(sqrtf(ax * ax + ay * ay + az * az) - 1);
    if ((dc.rest -= dt) <= 0 && j > 0.6f) dc.rest = 0.3f, dc_throw(-ax, ay); /* the way the hand jerked */
}

static void dc_show(void) /* place the image and its shadow for where it is now */
{
    float sc = (CUP_F + D_REST) / (CUP_F + dc.d), fl = (CUP_F + D_REST) / (CUP_F + CUP_FLOOR);
    if (dc.dirty) dc_draw(DIE_H * sc), dc.dirty = false;
    /* only the part with the cube in it goes to the screen: the image's empty margin would cost a blend every frame */
    int side = dc.ext * 2 < DIE_IMG ? dc.ext * 2 : DIE_IMG;
    lv_obj_set_size(dc.img, side, side);
    lv_obj_set_pos(dc.img, (int32_t)(SCR / 2 + dc.x * sc - side / 2), (int32_t)(SCR / 2 + dc.y * sc - side / 2));
    float up = fmaxf(0, D_REST - dc.d) / D_REST; /* 0 on the floor, 1 at the rim */
    int ss = (int)(DIE_H * 2.6f * fl * (1 + up * 0.6f));
    lv_obj_set_size(dc.shadow, ss, ss);
    lv_obj_set_pos(dc.shadow, (int32_t)(SCR / 2 + dc.x * fl - ss / 2), (int32_t)(SCR / 2 + dc.y * fl - ss / 2));
    lv_obj_set_style_bg_opa(dc.shadow, (lv_opa_t)(170 * (1 - up * 0.8f)), 0);
}

static void dc_step(lv_timer_t *t)
{
    (void)t;
    static uint32_t last;
    float dt = fminf(0.04f, fmaxf(0.004f, lv_tick_elaps(last) / 1000.0f)); /* by the clock: frames come when they can */
    last = lv_tick_get();
    float moved = 0;
    if (dc.phase == DC_FALL) {
        dc.vd += 2600 * dt;
        dc.x += dc.vx * dt, dc.y += dc.vy * dt, dc.d += dc.vd * dt;
        bool floor = false;
        if (dc.d >= D_REST) { /* the floor: it bounces lower each time, a knock tumbles it */
            dc.d = D_REST;
            if (dc.vd > 0) {
                float hit = dc.vd;
                dc.vd = hit > 150 ? -0.38f * hit : 0;
                if (hit > 150) /* a real bounce tumbles it; just lying there must not */
                    for (int i = 0; i < 3; i++) dc.w[i] += (lv_rand(0, 2000) / 1000.0f - 1) * hit / 90;
                if (hit > 180) voice_say(VOICE_TICK);
            }
            floor = dc.vd == 0;
        }
        float r = CUP_R - DIE_H * 1.3f, dd = hypotf(dc.x, dc.y);
        if (dd > r) { /* the wall */
            float nx = dc.x / dd, ny = dc.y / dd, vn = dc.vx * nx + dc.vy * ny;
            dc.x = nx * r, dc.y = ny * r;
            if (vn > 0) {
                dc.vx -= 1.6f * vn * nx, dc.vy -= 1.6f * vn * ny;
                for (int i = 0; i < 3; i++) dc.w[i] += (lv_rand(0, 2000) / 1000.0f - 1) * vn / 70;
                if (vn > 120) voice_say(VOICE_TICK);
            }
        }
        float speed = hypotf(dc.vx, dc.vy);
        if (floor) { /* on the floor it rolls the way it goes and slows down */
            float drag = expf(-dt * 2.6f), k = 1 - expf(-dt * 3.0f);
            dc.vx *= drag, dc.vy *= drag;
            float roll[3] = {dc.vy / DIE_H, -dc.vx / DIE_H, 0};
            if (speed < 70) for (int i = 0; i < 3; i++) dc.w[i] *= expf(-dt * 8); /* slow: it stops rolling and lies down */
            else for (int i = 0; i < 3; i++) dc.w[i] += (roll[i] - dc.w[i]) * k;
        }
        float wl = sqrtf(dc.w[0] * dc.w[0] + dc.w[1] * dc.w[1] + dc.w[2] * dc.w[2]);
        if (wl > 0.01f) dc_rotate(dc.w[0] / wl, dc.w[1] / wl, dc.w[2] / wl, wl * dt);
        if (floor && speed < 70 && dc_square(fminf(1, dt * 7)) < 0.02f && speed < 15)
            dc.phase = DC_REST, dc.t = 0.35f, dc.vx = dc.vy = dc.vd = 0, memset(dc.w, 0, sizeof dc.w);
        moved = 1;
    } else if (dc.phase == DC_REST && (dc.t -= dt) <= 0) {
        dc.phase = DC_RISE, dc.t = 0, dc.from[0] = dc.x, dc.from[1] = dc.y, dc.from[2] = dc.d;
    } else if (dc.phase == DC_RISE) { /* up to you, to the middle, edges square: easy to read */
        dc.t = fminf(1, dc.t + dt / 0.8f);
        float e = dc.t * dc.t * (3 - 2 * dc.t);
        dc.x = dc.from[0] * (1 - e), dc.y = dc.from[1] * (1 - e), dc.d = dc.from[2] + (D_SHOW - dc.from[2]) * e;
        dc_straighten(fminf(1, dt * 6));
        if (dc.t >= 1) dc.phase = DC_SHOW;
        moved = 1, dc.dirty = true; /* it grows */
    }
    if (moved || dc.dirty) dc_show();
}

static bool dc_button(int id) { if (id != PIP_BTN_PWR) return false; dc_throw(0, 0); return true; }
static void dc_close(void) { if (dc.timer) lv_timer_delete(dc.timer), dc.timer = NULL; }

static void dc_open(lv_obj_t *scr)
{
    lv_draw_buf_t *buf = dc.buf;
    memset(&dc, 0, sizeof dc);
    dc.buf = buf ? buf : lv_draw_buf_create(DIE_IMG, DIE_IMG, LV_COLOR_FORMAT_ARGB8888, 0);
    lv_draw_buf_clear(dc.buf, NULL);
    dc.ext = DIE_IMG / 2;
    /* the cup, drawn once into the screen's background: the floor lit a little, the wall darkening up to the rim
       (dithered so the dark steps do not band; big circles as objects cost the board ~20 ms a frame under the die) */
    static lv_draw_buf_t *cup;
    static uint32_t cup_for = 0xFFFFFFFF;
    if (!cup) cup = lv_draw_buf_create(SCR, SCR, LV_COLOR_FORMAT_RGB565, 0);
    if (cup_for != (th->bg ^ th->panel << 1)) {
        cup_for = th->bg ^ th->panel << 1;
        static const uint8_t bayer[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
        uint16_t *px = (uint16_t *)cup->data;
        uint32_t stride = cup->header.stride / 2;
        for (int y = 0; y < SCR; y++)
            for (int x = 0; x < SCR; x++) {
                float r = hypotf(x + 0.5f - SCR / 2.0f, y + 0.5f - SCR / 2.0f), t = fmaxf(0, fminf(1, (r - 150) / 83));
                t = powf(t, 0.7f);
                uint32_t q[3]; /* 5 bits a channel, green too: its sixth bit alone tints the dark greys green */
                for (int k = 0; k < 3; k++) {
                    float v = ((th->panel >> (16 - k * 8)) & 255) * (1 - t) + ((th->bg >> (16 - k * 8)) & 255) * t + bayer[y & 3][x & 3] / 2.0f;
                    q[k] = (uint32_t)fminf(255, v) >> 3;
                }
                px[y * stride + x] = (uint16_t)(q[0] << 11 | (q[1] << 1 | q[1] >> 4) << 5 | q[2]);
            }
    }
    lv_obj_set_style_bg_image_src(scr, cup, 0);
    dc.shadow = th_box(scr, 0, 0, 10, 10, LV_RADIUS_CIRCLE, 0x000000);
    for (int i = 0; i < 3; i++) dc.R[i][i] = 1;
    /* it starts up near you, showing a random face square on */
    for (int i = 0; i < 4; i++) {
        float ax = lv_rand(0, 1000) / 1000.0f, ay = lv_rand(0, 1000) / 1000.0f, l = sqrtf(ax * ax + ay * ay + 0.25f);
        dc_rotate(ax / l, ay / l, 0.5f / l, lv_rand(0, 6283) / 1000.0f);
    }
    for (int i = 0; i < 40 && dc_square(1) > 0.001f; i++) {}
    for (int i = 0; i < 40; i++) dc_straighten(1);
    dc.d = D_SHOW, dc.phase = DC_SHOW, dc.dirty = true;
    dc.img = lv_image_create(scr);
    lv_image_set_src(dc.img, dc.buf);
    lv_image_set_inner_align(dc.img, LV_IMAGE_ALIGN_CENTER);
    dc_show();
    dc.timer = lv_timer_create(dc_step, 16, NULL);
}

/* ================= the list: the first one sits in the middle of the honeycomb ================= */

const app_t apps[] = {
    {.id = "clock", .name = "Часы", .icon = ICON_CLOCK, .color = 0x0091FF, .open = clock_open, .close = clock_close},
    {.id = "care", .name = "Пип", .icon = ICON_PIP, .color = 0x00DAC3, .open = care_open, .close = care_close},
    {.id = "qr", .name = "Quick QR", .icon = ICON_QR, .color = 0x48484A, .open = qr_open, .close = qr_close, .awake = true},
    {.id = "noise", .name = "Шумомер", .icon = ICON_NOISE, .color = 0x34C759, .open = nz_open, .close = nz_close, .awake = true},
    {.id = "badge", .name = "Бейдж", .icon = ICON_BADGE, .color = 0x6D7CFF, .open = badge_open, .button = badge_button, .awake = true},
    {.id = "timer", .name = "Таймер", .icon = ICON_TIMER, .color = 0xFF9230, .open = timer_open, .close = timer_close,
     .button = timer_button, .awake = true},
    {.id = "stopwatch", .name = "Секундомер", .icon = ICON_STOPWATCH, .color = 0xFF4245, .open = sw_open, .close = sw_close,
     .button = sw_button, .awake = true},
    {.id = "torch", .name = "Фонарик", .icon = ICON_TORCH, .color = 0xFFD600, .open = torch_open, .awake = true},
    {.id = "games", .name = "Игры", .icon = ICON_GAME, .color = 0xDB34F2, .open = games_open},
    {.id = "style", .name = "Стиль", .icon = ICON_THEME, .color = 0x3CD3FE, .open = style_open, .button = style_button},
    {.id = "notify", .name = "Уведомления", .icon = ICON_BELL, .color = 0xFF375F, .open = phone_notify_open, .close = phone_notify_close,
     .button = phone_notify_button, .vertical = true},
    {.id = "music", .name = "Музыка", .icon = ICON_MUSIC, .color = 0xFF375F, .open = phone_music_open, .close = phone_music_close,
     .button = phone_music_button, .vertical = true},
    {.id = "camera", .name = "Камера", .icon = ICON_CAMERA, .color = 0x30D158, .open = phone_camera_open, .close = phone_camera_close,
     .button = phone_camera_button, .awake = true},
    {.id = "bt", .name = "Bluetooth", .icon = ICON_BT, .color = 0x0A84FF, .open = phone_bt_open, .close = phone_bt_close},
    {.id = "remote", .name = "Пульт", .icon = ICON_REMOTE, .color = 0x5E5CE6, .open = phone_remote_open, .close = phone_remote_close,
     .button = phone_remote_button,
     .button_up = phone_remote_button_up, .imu = phone_remote_imu, .gyro = phone_remote_gyro, .awake = true},
    /* the games, listed by "games" and not in the menu */
    {.id = "ball", .name = "Шарик", .icon = ICON_GAME, .color = 0xDB34F2, .open = game_open, .close = game_close, .imu = game_imu,
     .button = game_button, .group = "games"},
    {.id = "xo", .name = "Крестики-нолики", .icon = ICON_XO, .open = xo_open, .close = xo_close, .group = "games"},
    {.id = "eight", .name = "Шар-восьмёрка", .icon = ICON_EIGHT, .open = e8_open, .close = e8_close, .imu = e8_imu, .button = e8_button,
     .group = "games"},
    {.id = "coin", .name = "Монетка", .icon = ICON_COIN, .open = cn_open, .close = cn_close, .imu = cn_imu, .button = cn_button,
     .group = "games"},
    {.id = "dice", .name = "Кубик", .icon = ICON_DICE, .open = dc_open, .close = dc_close, .imu = dc_imu, .button = dc_button,
     .group = "games"},
    {.id = "maze", .name = "Лабиринт", .icon = ICON_MAZE, .open = mz_open, .close = mz_close, .imu = mz_imu, .button = mz_button,
     .group = "games"},
};
const int app_total = sizeof apps / sizeof apps[0];
const int app_count = app_total - 6; /* the games above are not in the menu */

void apps_init(void) { lv_timer_create(timer_watch, 250, NULL); }
