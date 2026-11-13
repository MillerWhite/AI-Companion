#include "shell.h"
#include "doodle.h"
#include "phone.h"
#include "pip.h"
#include "theme.h"
#include "voice.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define SCR 466
#define ICON 150           /* icon sprite size */
#define CAR_SPACING 150.0f /* carousel: px between icon centres */
#define CAR_Y 196          /* carousel: centre of the middle icon */
#define HEX_SPACING 128.0f /* honeycomb: px between neighbours */
#define HEX_ICON 118.0f    /* honeycomb: icon size in the middle */
#define HEX_R 205.0f       /* honeycomb: icons shrink from here outward */
#define ROW 76.0f          /* list: px between rows: a fingertip each */
#define LENS_X 80          /* list: the focus plate, rows read through it */
#define LENS_W 320
#define LENS_H 56

static struct {
    lv_obj_t *pip_scr, *menu_scr, *app_scr, *name;
    lv_obj_t *icons[24], *rows[24], *marker;
    lv_obj_t *bat, *bat_fill; /* the battery at the top of the menu */
    bool updating;            /* shell_updating(): the screen stays lit until the reset */
    lv_obj_t *lens, *lens_rows[24], *lens_cursor, *chrome, *head, *scroll; /* the list's plate; the terminal's header and scroll cells */
    th_cell_t scroll_cells[24];
    int tick_row; /* tick_row: the wheel clicks each time a new row reaches the middle */
    lv_draw_buf_t *sprites[ICON_COUNT];
    int open;  /* app index, -1 = none */
    int style; /* MENU_* */
    /* carousel */
    float pos, drag_from, anim_from, anim_to;
    /* honeycomb */
    float hx[24], hy[24], pan_x, pan_y, vx, vy, min_x, max_x, min_y, max_y;
    lv_timer_t *glide;
    uint32_t last_empty_tap;
    /* input */
    lv_point_t press, last;
    uint32_t press_tick, last_tick;
    int64_t now;
    int tz;
    /* the list shows apps lo..lo+n-1: the menu, or a group (the games) in place of it */
    int lo, n;
    const char *group;
    float saved_pos; /* where the menu was before the group */
} sh = {.open = -1, .style = MENU_HONEYCOMB};

static bool icons_stale = true;
static void icons_build(void) /* the honeycomb's and carousel's sprites: SDF art, slow, so only when needed */
{
    if (!icons_stale) return;
    icons_stale = false;
    int style = th->icons == ICONS_GLASS ? ICON_STYLE_GLASS : th->icons == ICONS_BLOCKS ? ICON_STYLE_BLOCKS : ICON_STYLE_OUTLINE;
    for (int i = 0; i < app_count; i++) {
        uint32_t disc = style == ICON_STYLE_GLASS ? apps[i].color : th->panel;
        doodle_icon(apps[i].icon, style, disc, th->text, th->faint, (uint32_t *)sh.sprites[apps[i].icon]->data, ICON, ICON);
        lv_obj_invalidate(sh.icons[i]);
    }
}
lv_draw_buf_t *shell_icon(int icon)
{
    icons_build();
    return sh.sprites[icon];
}
int64_t shell_local_time(void) { return sh.now + (int64_t)sh.tz * 60; }

static struct { int pct; bool charging; } bat = {-1, false};
static void bat_show(void)
{
    if (!sh.bat) return;
    lv_obj_set_flag(sh.bat, LV_OBJ_FLAG_HIDDEN, bat.pct < 0);
    if (bat.pct < 0) return;
    uint32_t c = bat.pct < 20 ? 0xFF453A : bat.pct < 50 ? 0xFF9F0A : 0x34C759;
    lv_obj_set_width(sh.bat_fill, 2 + 26 * bat.pct / 100);
    lv_obj_set_style_bg_color(sh.bat_fill, lv_color_hex(c), 0);
}
void shell_battery(int percent, bool charging)
{
    bool changed = percent != bat.pct;
    bat.pct = percent, bat.charging = charging, pip_battery(percent, charging);
    if (changed) bat_show();
}
int shell_battery_level(bool *charging)
{
    if (charging) *charging = bat.charging;
    return bat.pct;
}
const char *shell_battery_text(void)
{
    static char t[8];
    if (bat.pct < 0) return "--";
    lv_snprintf(t, sizeof t, "%d%%%s", bat.pct, bat.charging ? "+" : "");
    return t;
}
int shell_menu_style(void) { return sh.style; }

const char *shell_where(void)
{
    const char *over = phone_where(); /* a call, the pairing or a banner on top */
    if (over) return over;
    lv_obj_t *a = lv_screen_active();
    return a == sh.pip_scr ? "pip" : a == sh.menu_scr ? "menu" : sh.open >= 0 ? apps[sh.open].id : "?";
}

static void open_app(int i);
static void group_leave(void);

/* ---------- carousel (the first menu, kept as an option) ---------- */

static float wrap(float d) { return d - app_count * roundf(d / app_count); }
static int car_middle(void) { return ((int)roundf(sh.pos) % app_count + app_count) % app_count; }

static void car_layout(void)
{
    for (int i = 0; i < app_count; i++) {
        float d = wrap(i - sh.pos), ad = fabsf(d);
        lv_obj_t *img = sh.icons[i];
        if (ad > 1.9f) { lv_obj_add_flag(img, LV_OBJ_FLAG_HIDDEN); continue; }
        lv_obj_remove_flag(img, LV_OBJ_FLAG_HIDDEN);
        /* icons ride a gentle arc: the middle one high and big, the neighbours lower and smaller */
        lv_obj_set_pos(img, (int32_t)(SCR / 2 + d * CAR_SPACING - ICON / 2), (int32_t)(CAR_Y + d * d * 26 - ICON / 2));
        lv_image_set_scale(img, (uint32_t)(256 * (1 - fminf(ad, 2) * 0.32f)));
        lv_obj_set_style_opa(img, (lv_opa_t)(255 * (1 - fminf(ad, 1.5f) * 0.45f)), 0);
    }
    lv_label_set_text(sh.name, th_label(apps[car_middle()].name));
    lv_obj_set_style_opa(sh.name, (lv_opa_t)(255 * fmaxf(0, 1 - 2.5f * fabsf(sh.pos - roundf(sh.pos)))), 0);
}

static void layout(void);

static void snap_exec(void *var, int32_t v)
{
    (void)var;
    sh.pos = sh.anim_from + (sh.anim_to - sh.anim_from) * v / 1000.0f;
    layout();
}

static void snap_ms(float target, uint32_t ms)
{
    sh.anim_from = sh.pos;
    sh.anim_to = target;
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, &sh);
    lv_anim_set_exec_cb(&a, snap_exec);
    lv_anim_set_values(&a, 0, 1000);
    lv_anim_set_duration(&a, ms);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_start(&a);
}

static void snap_to(float target) { snap_ms(target, 260); }

static void car_input(lv_event_code_t code, lv_point_t p)
{
    int dx = p.x - sh.press.x, dy = p.y - sh.press.y;
    if (code == LV_EVENT_PRESSED) {
        lv_anim_delete(&sh, snap_exec);
        sh.drag_from = sh.pos;
    } else if (code == LV_EVENT_PRESSING) {
        if (abs(dx) > abs(dy)) { sh.pos = sh.drag_from - dx / CAR_SPACING; car_layout(); }
    } else if (code == LV_EVENT_RELEASED) {
        uint32_t ms = lv_tick_elaps(sh.press_tick);
        if (dy > 70 && abs(dx) < dy) { snap_to(roundf(sh.drag_from)); shell_home(); return; } /* swipe down: back to Pip */
        if (abs(dx) < 12 && abs(dy) < 12) {                                                 /* tap */
            if (hypotf(p.x - SCR / 2, p.y - CAR_Y) < ICON / 2 + 10) open_app(car_middle());
            else snap_to(roundf(sh.pos) + (p.x > SCR / 2 ? 1 : -1)); /* tap a neighbour: bring it in */
            return;
        }
        float target = roundf(sh.pos);
        if (ms < 300 && abs(dx) > 40 && target == roundf(sh.drag_from)) target -= dx > 0 ? 1 : -1; /* a flick moves one */
        snap_to(target);
    }
}

/* ---------- honeycomb, like a watch: pan anywhere, icons shrink toward the rim ---------- */

/* where icon i is on screen (centre, relative to the screen centre) and how big, after the fisheye */
static float hex_place(int i, float *x, float *y)
{
    float px = sh.hx[i] + sh.pan_x, py = sh.hy[i] + sh.pan_y, d = hypotf(px, py), t = d / HEX_R;
    float edge = fmaxf(0, t - 0.5f);
    float pull = 1 - 0.22f * edge;          /* the rim pulls icons in, packing them tighter */
    *x = px * pull, *y = py * pull;
    return fmaxf(0, 1 - edge * 0.9f); /* full size in the middle, small at the rim, gone past it */
}

static int hex_focus(void) /* the icon closest to the middle */
{
    int best = 0;
    float bd = 1e9f;
    for (int i = 0; i < app_count; i++) {
        float d = hypotf(sh.hx[i] + sh.pan_x, sh.hy[i] + sh.pan_y);
        if (d < bd) bd = d, best = i;
    }
    return best;
}

static void hex_layout(void)
{
    for (int i = 0; i < app_count; i++) {
        float x, y, k = hex_place(i, &x, &y);
        lv_obj_t *img = sh.icons[i];
        if (k < 0.08f) { lv_obj_add_flag(img, LV_OBJ_FLAG_HIDDEN); continue; }
        lv_obj_remove_flag(img, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_pos(img, (int32_t)(SCR / 2 + x - ICON / 2), (int32_t)(SCR / 2 + y - ICON / 2));
        lv_image_set_scale(img, (uint32_t)(256 * HEX_ICON / ICON * k));
        lv_obj_set_style_opa(img, LV_OPA_COVER, 0);
    }
    int f = hex_focus();
    lv_label_set_text(sh.name, th_label(apps[f].name));
    float d = hypotf(sh.hx[f] + sh.pan_x, sh.hy[f] + sh.pan_y);
    lv_obj_set_style_opa(sh.name, (lv_opa_t)(255 * fmaxf(0, 1 - d / 50)), 0);
}

static void hex_clamp(void)
{
    sh.pan_x = fmaxf(-sh.max_x, fminf(-sh.min_x, sh.pan_x));
    sh.pan_y = fmaxf(-sh.max_y, fminf(-sh.min_y, sh.pan_y));
}

static void hex_glide(lv_timer_t *t) /* inertia after a flick */
{
    (void)t;
    if (lv_screen_active() != sh.menu_scr || sh.style != MENU_HONEYCOMB || (fabsf(sh.vx) < 0.02f && fabsf(sh.vy) < 0.02f)) return;
    sh.pan_x += sh.vx * 16, sh.pan_y += sh.vy * 16;
    sh.vx *= 0.9f, sh.vy *= 0.9f;
    hex_clamp();
    hex_layout();
}

static void hex_input(lv_event_code_t code, lv_point_t p)
{
    if (code == LV_EVENT_PRESSED) {
        sh.vx = sh.vy = 0;
    } else if (code == LV_EVENT_PRESSING) {
        uint32_t el = lv_tick_elaps(sh.last_tick);
        float dx = p.x - sh.last.x, dy = p.y - sh.last.y;
        sh.pan_x += dx, sh.pan_y += dy;
        if (el > 0) sh.vx = 0.6f * sh.vx + 0.4f * dx / el, sh.vy = 0.6f * sh.vy + 0.4f * dy / el; /* px per ms */
        hex_clamp();
        hex_layout();
    } else if (code == LV_EVENT_RELEASED) {
        if (abs(p.x - sh.press.x) > 12 || abs(p.y - sh.press.y) > 12) return; /* it was a drag: let it glide */
        sh.vx = sh.vy = 0;
        int hit = -1;
        float best = 0;
        for (int i = 0; i < app_count; i++) {
            float x, y, k = hex_place(i, &x, &y);
            if (k > best && hypotf(p.x - SCR / 2 - x, p.y - SCR / 2 - y) < HEX_ICON / 2 * k) best = k, hit = i;
        }
        if (hit >= 0) { open_app(hit); return; }
        if (lv_tick_elaps(sh.last_empty_tap) < 400) shell_home(); /* double tap on nothing: back to Pip */
        sh.last_empty_tap = lv_tick_get();
    }
}

/* ---------- vertical list: a rotary dial (Приборка) or a terminal (Диагностика) ---------- */

static int list_middle(void) { return (int)fmaxf(0, fminf(sh.n - 1, roundf(sh.pos))); }

/* size and ink by distance from the middle, continuous: 36 px in the plate, 26 one row away, 20 two away */
static float row_scale(float ad) { return (ad < 1 ? 36 - ad * 10 : ad < 2 ? 26 - (ad - 1) * 6 : 20) / 36.0f; }

/* Area-averaging downscale of an alpha mask by f (0.5..1): separable, each output pixel covers at most three source
   pixels a side, weights in 1/256. Fast enough for the ESP32 (it runs when the theme changes). */
#define SHRINK_MAX 512
static void shrink_weights(int n_out, float f, int n_in, int16_t idx[][3], uint8_t wt[][3])
{
    for (int o = 0; o < n_out; o++) {
        float a = o / f, b = (o + 1) / f;
        int t = 0;
        for (int s = (int)a; s < (int)ceilf(b) && s < n_in && t < 3; s++, t++) {
            float cover = fminf(b, s + 1) - fmaxf(a, s);
            idx[o][t] = (int16_t)s, wt[o][t] = (uint8_t)lroundf(cover * f * 255);
        }
        for (; t < 3; t++) idx[o][t] = 0, wt[o][t] = 0;
    }
}

static void shrink(const uint8_t *src, int sstride, int w, int h, uint8_t *dst, int dstride, int mw, int mh, float f)
{
    static int16_t xi[SHRINK_MAX][3], yi[SHRINK_MAX][3];
    static uint8_t xw[SHRINK_MAX][3], yw[SHRINK_MAX][3];
    if (mw > SHRINK_MAX || mh > SHRINK_MAX) return;
    shrink_weights(mw, f, w, xi, xw);
    shrink_weights(mh, f, h, yi, yw);
    for (int y = 0; y < mh; y++)
        for (int x = 0; x < mw; x++) {
            uint32_t v = 0;
            for (int j = 0; j < 3; j++) {
                const uint8_t *row = src + yi[y][j] * sstride;
                v += yw[y][j] * (uint32_t)(row[xi[x][0]] * xw[x][0] + row[xi[x][1]] * xw[x][1] + row[xi[x][2]] * xw[x][2]);
            }
            v = (v + 32512) / 65025; /* both weights were x 255 */
            dst[y * dstride + x] = (uint8_t)(v > 255 ? 255 : v);
        }
}

/* Each row is drawn once, as an alpha mask in the big font, and shrunk ahead of time to every size from 20 to 36 px:
   scrolling then only picks a mask and tints it. (Scaling text live made LVGL render every row into a layer and
   transform it each frame: ~45 ms a menu frame on the board.) */
#define ROW_PX_MIN 20
#define ROW_PX_MAX 36
#define ROW_SIZES (ROW_PX_MAX - ROW_PX_MIN + 1)
static lv_draw_buf_t *row_mask[24][ROW_SIZES];

static void rows_build(void)
{
    /* an off-screen canvas and one scratch mask, wide enough for any row, to draw the text with */
    static lv_obj_t *pen;
    static lv_draw_buf_t *base;
    const lv_font_t *font = th->title ? th->title : th->body; /* 40 px capitals, shrunk; never scaled up */
    int h = lv_font_get_line_height(font), big = th->title ? 40 : 28; /* the font's px size */
    if (base && base->header.h != h) lv_draw_buf_destroy(base), base = NULL;
    if (!base) base = lv_draw_buf_create(SCR, h, LV_COLOR_FORMAT_A8, 0);
    LV_ASSERT_MALLOC(base);
    if (!pen) pen = lv_canvas_create(sh.menu_scr), lv_obj_add_flag(pen, LV_OBJ_FLAG_HIDDEN);
    lv_canvas_set_draw_buf(pen, base);
    for (int i = 0; i < app_total; i++) {
        char text[48];
        if (apps[i].group) lv_snprintf(text, sizeof text, "%s", apps[i].name); /* a group's rows wear a small icon instead */
        else lv_snprintf(text, sizeof text, "%02d  %s", i + 1, apps[i].name);
        const char *t = th_label(text);
        lv_point_t sz;
        lv_text_get_size(&sz, t, font, th->caps, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
        int isz = apps[i].group ? h : 0, ix = isz ? isz + h / 4 : 0;
        int w = ix + sz.x + 2 < SCR ? ix + sz.x + 2 : SCR;
        lv_draw_buf_clear(base, NULL);
        lv_layer_t layer;
        lv_canvas_init_layer(pen, &layer);
        lv_draw_label_dsc_t dsc;
        lv_draw_label_dsc_init(&dsc);
        dsc.font = font, dsc.text = t, dsc.letter_space = th->caps, dsc.color = lv_color_white();
        lv_area_t a = {ix, 0, w - 1, h - 1};
        lv_draw_label(&layer, &dsc, &a);
        lv_canvas_finish_layer(pen, &layer);
        if (isz) doodle_icon_mask(apps[i].icon, base->data + (h - isz) / 2 * base->header.stride, (int)base->header.stride, isz);
        for (int k = 0; k < ROW_SIZES; k++) { /* area-averaged down to each size */
            if (row_mask[i][k]) lv_draw_buf_destroy(row_mask[i][k]);
            float f = (float)(ROW_PX_MIN + k) / big;
            int mw = (int)lroundf(w * f), mh = (int)lroundf(h * f);
            mw = mw < 1 ? 1 : mw, mh = mh < 1 ? 1 : mh;
            lv_draw_buf_t *m = row_mask[i][k] = lv_draw_buf_create(mw, mh, LV_COLOR_FORMAT_A8, 0);
            LV_ASSERT_MALLOC(m);
            shrink(base->data, base->header.stride, w, h, m->data, m->header.stride, mw, mh, f);
        }
    }
}

static lv_draw_buf_t *row_at(int i, float ad)
{
    int k = (int)lroundf(row_scale(ad) * ROW_PX_MAX) - ROW_PX_MIN;
    return row_mask[i][k < 0 ? 0 : k >= ROW_SIZES ? ROW_SIZES - 1 : k];
}

static void list_layout(void)
{
    bool terminal = TH_TERMINAL;
    float settle = fabsf(sh.pos - roundf(sh.pos));
    for (int i = 0; i < app_total; i++) {
        float d = i - sh.lo - sh.pos, ad = fabsf(d);
        lv_obj_t *r = sh.rows[i], *l = sh.lens_rows[i];
        if (i < sh.lo || i >= sh.lo + sh.n) { lv_obj_add_flag(r, LV_OBJ_FLAG_HIDDEN), lv_obj_add_flag(l, LV_OBJ_FLAG_HIDDEN); continue; }
        bool far = ad > (TH_TERMINAL ? 1.9f : 2.3f); /* past these it is off the dial (or faded out): not drawn at all */
        lv_obj_set_flag(r, LV_OBJ_FLAG_HIDDEN, far);
        lv_obj_set_flag(l, LV_OBJ_FLAG_HIDDEN, ad > 1.2f);
        if (far) continue;
        /* the row's pre-shrunk mask for this distance: it grows and shrinks a pixel at a time */
        lv_draw_buf_t *m = row_at(i, ad);
        if (lv_image_get_src(r) != m) lv_image_set_src(r, m), lv_image_set_src(l, m);
        lv_color_t ink = lv_color_mix(lv_color_hex(terminal ? th->mute : th->faint), lv_color_hex(th->dim), (uint8_t)(255 * fminf(fmaxf(ad - 1, 0), 1)));
        lv_obj_set_style_image_recolor(r, ink, 0);
        /* the plate is see-through there: hand the row over to it; the terminal fades rows out before its key strip */
        float fade = terminal ? fminf(1, fmaxf(0, (1.9f - ad) * 2)) : fminf(1, ad);
        lv_obj_set_style_opa(r, (lv_opa_t)(255 * fade), 0);
        lv_obj_align(r, LV_ALIGN_LEFT_MID, LENS_X + 16, (int32_t)roundf(d * ROW));
        lv_obj_align(l, LV_ALIGN_LEFT_MID, 16, (int32_t)roundf(d * ROW));
    }
    int mid = list_middle();
    if (mid != sh.tick_row) { /* a detent */
        sh.tick_row = mid;
        if (lv_screen_active() == sh.menu_scr) voice_say(VOICE_TICK);
    }
    if (terminal) {
        lv_obj_set_flag(sh.lens_cursor, LV_OBJ_FLAG_HIDDEN, settle > 0.02f);
        lv_obj_update_layout(sh.lens_rows[sh.lo + mid]);
        lv_obj_align_to(sh.lens_cursor, sh.lens_rows[sh.lo + mid], LV_ALIGN_OUT_RIGHT_MID, 6, 0);
        for (int i = 0; i < sh.n; i++) th_cell_set(sh.scroll, &sh.scroll_cells[i], i == mid ? th->text : th->off);
        char t[48];
        if (sh.group) lv_snprintf(t, sizeof t, "игры // %02d/%02d", mid + 1, sh.n);
        else lv_snprintf(t, sizeof t, "заряд %s // %02d/%02d", shell_battery_text(), mid + 1, sh.n);
        const char *u = th_label(t);
        if (strcmp(u, lv_label_get_text(sh.head))) lv_label_set_text(sh.head, u); /* also as the battery changes */
    } else lv_obj_add_flag(sh.lens_cursor, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_flag(sh.marker, LV_OBJ_FLAG_HIDDEN, terminal);
}

/* a picker wheel: drag it, flick it and it coasts, slowing down to land exactly on a row */
static void list_input(lv_event_code_t code, lv_point_t p)
{
    int dx = p.x - sh.press.x, dy = p.y - sh.press.y;
    if (code == LV_EVENT_PRESSED) {
        lv_anim_delete(&sh, snap_exec); /* a touch catches a coasting wheel */
        sh.drag_from = sh.pos;
        sh.vy = 0;
    } else if (code == LV_EVENT_PRESSING) {
        uint32_t el = lv_tick_elaps(sh.last_tick);
        if (el > 0) sh.vy = 0.6f * sh.vy + 0.4f * (p.y - sh.last.y) / el; /* px per ms */
        if (abs(dy) > abs(dx)) {
            float pos = sh.drag_from - dy / ROW, lo = 0, hi = sh.n - 1;
            /* past either end it pulls back like a rubber band */
            if (pos < lo) pos = lo - (lo - pos) / (1 + (lo - pos) * 2);
            if (pos > hi) pos = hi + (pos - hi) / (1 + (pos - hi) * 2);
            sh.pos = pos;
            list_layout();
        }
    } else if (code == LV_EVENT_RELEASED) {
        if (dx > 90 && abs(dy) < dx) { /* swipe right: out of a group, or back to Pip */
            if (sh.group) group_leave();
            else snap_to((float)list_middle()), shell_home();
            return;
        }
        if (abs(dx) < 12 && abs(dy) < 12) {                                                  /* tap a row */
            int row = (int)roundf(sh.pos + (p.y - SCR / 2) / ROW);
            if (row == list_middle() && fabsf(sh.pos - row) < 0.1f) open_app(sh.lo + row);
            else if (row >= 0 && row < sh.n) snap_to((float)row);
            return;
        }
        if (lv_tick_elaps(sh.last_tick) > 60) sh.vy = 0; /* held still before letting go: no throw */
        /* where a wheel losing ~0.3% of its speed per ms would stop, rounded to a row */
        float target = roundf(sh.pos - sh.vy * 330 / ROW);
        target = fmaxf(0, fminf(sh.n - 1, target));
        float rows = fabsf(target - sh.pos);
        snap_ms(target, (uint32_t)fminf(1200, 260 + 110 * rows));
    }
}

/* ---------- the menu screen ---------- */

static void layout(void)
{
    if (sh.style == MENU_HONEYCOMB) hex_layout();
    else if (sh.style == MENU_CAROUSEL) car_layout();
    else list_layout();
}

static void menu_input(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_point_t p;
    lv_indev_get_point(lv_indev_active(), &p);
    if (code == LV_EVENT_PRESSED) sh.press = p, sh.press_tick = lv_tick_get();
    if (sh.style == MENU_HONEYCOMB) hex_input(code, p);
    else if (sh.style == MENU_CAROUSEL) car_input(code, p);
    else list_input(code, p);
    sh.last = p, sh.last_tick = lv_tick_get();
}

void shell_set_menu_style(int style)
{
    sh.style = style;
    sh.vx = sh.vy = 0;
    if (style != MENU_LIST) icons_build();
    bool list = style == MENU_LIST;
    if (!list && sh.group) sh.group = NULL, sh.lo = 0, sh.n = app_count, sh.pos = sh.saved_pos; /* groups are the list's */
    for (int i = 0; i < app_total; i++) {
        if (i < app_count) lv_obj_set_flag(sh.icons[i], LV_OBJ_FLAG_HIDDEN, list);
        lv_obj_set_flag(sh.rows[i], LV_OBJ_FLAG_HIDDEN, !list);
    }
    lv_obj_set_flag(sh.marker, LV_OBJ_FLAG_HIDDEN, !list);
    lv_obj_set_flag(sh.lens, LV_OBJ_FLAG_HIDDEN, !list);
    lv_obj_set_flag(sh.chrome, LV_OBJ_FLAG_HIDDEN, !(list && TH_TERMINAL));
    lv_obj_set_flag(sh.name, LV_OBJ_FLAG_HIDDEN, list);
    lv_obj_align(sh.name, LV_ALIGN_TOP_MID, 0, style == MENU_HONEYCOMB ? 396 : CAR_Y + ICON / 2 + 22);
    lv_obj_set_style_text_font(sh.name, style == MENU_HONEYCOMB ? th->small : th->body, 0);
    if (list) sh.pos = (float)list_middle(), sh.tick_row = list_middle(); /* no click for setting it */
    layout();
}

static void chrome_build(void) /* the terminal's header, hints and scroll cells, for what the list shows */
{
    lv_obj_clean(sh.chrome);
    if (!TH_TERMINAL) return;
    sh.head = th_header(sh.chrome, "", 40);
    th_footer(sh.chrome, sh.group ? TH_UP " открыть   " TH_DOWN " назад" : TH_UP " открыть   " TH_DOWN " пип", 392);
    int top = SCR / 2 - (sh.n * 11 - 2) / 2;
    for (int i = 0; i < sh.n; i++) sh.scroll_cells[i] = (th_cell_t){405, (int16_t)(top + i * 11), 9, 9, th->off};
    sh.scroll = th_cells(sh.chrome, sh.scroll_cells, sh.n);
}

/* a group (the games) takes the list's place; pos: the row to land on */
static void group_enter(const char *id, int pos)
{
    if (!sh.group) sh.saved_pos = sh.pos;
    sh.group = id, sh.lo = -1, sh.n = 0;
    for (int i = app_count; i < app_total; i++)
        if (apps[i].group && !strcmp(apps[i].group, id)) sh.lo = sh.lo < 0 ? i : sh.lo, sh.n++;
    sh.pos = (float)pos, sh.tick_row = pos;
    chrome_build();
    layout();
}

static void group_leave(void)
{
    sh.group = NULL, sh.lo = 0, sh.n = app_count;
    sh.pos = sh.saved_pos, sh.tick_row = (int)sh.saved_pos;
    chrome_build();
    layout();
}

static bool group_owner(int i) /* some apps are listed under this one */
{
    for (int k = app_count; k < app_total; k++)
        if (apps[k].group && !strcmp(apps[k].group, apps[i].id)) return true;
    return false;
}

/* repaint the menu for the current theme: background, icon style, fonts, the list rows */
static void restyle_menu(void)
{
    th_screen(sh.menu_scr);
    icons_stale = true; /* drawn when a menu style that shows them comes up */
    for (int i = 0; i < app_total; i++) {
        lv_obj_set_style_image_recolor(sh.lens_rows[i], lv_color_hex(TH_TERMINAL ? th->bg : th->text), 0);
        lv_image_set_src(sh.rows[i], NULL), lv_image_set_src(sh.lens_rows[i], NULL); /* the masks are about to change */
    }
    rows_build();
    lv_obj_set_style_bg_opa(sh.lens, TH_TERMINAL ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_color(sh.lens, lv_color_hex(th->text), 0);
    lv_obj_set_style_bg_color(sh.lens_cursor, lv_color_hex(th->bg), 0);
    chrome_build();
    lv_obj_set_style_bg_color(sh.marker, lv_color_hex(th->accent), 0);
    lv_obj_set_style_text_color(sh.name, lv_color_hex(th->text), 0);
    shell_set_menu_style(th->menu);
}

/* load a screen: the terminal scans it in top-down, the other themes slide or fade */
static void go(lv_obj_t *scr, lv_screen_load_anim_t anim, uint32_t ms, bool delete_old)
{
    if (!TH_TERMINAL) { lv_screen_load_anim(scr, anim, ms, 0, delete_old); return; }
    lv_obj_t *old = lv_screen_active();
    th_wipe(scr, lv_color_to_u32(lv_obj_get_style_bg_color(scr, 0)) & 0xFFFFFF);
    if (delete_old && old != scr) lv_obj_delete_async(old);
}

static void show_menu(lv_screen_load_anim_t anim)
{
    layout();
    go(sh.menu_scr, anim, 250, false);
}

/* ---------- apps ---------- */

static void back_to_menu(void);

static void app_gesture(lv_event_t *e)
{
    (void)e;
    if (sh.open >= 0 && apps[sh.open].vertical) return;
    if (lv_indev_get_gesture_dir(lv_indev_active()) == LV_DIR_BOTTOM) back_to_menu();
}

static void close_app(void)
{
    if (sh.open < 0) return;
    if (apps[sh.open].close) apps[sh.open].close();
    sh.open = -1;
}

static void open_app(int i)
{
    if (sh.style == MENU_LIST && group_owner(i)) { /* the list turns into the group */
        group_enter(apps[i].id, 0);
        if (lv_screen_active() != sh.menu_scr) show_menu(LV_SCREEN_LOAD_ANIM_FADE_IN);
        return;
    }
    lv_obj_t *old = sh.open >= 0 ? sh.app_scr : NULL; /* jumping from one app straight into another */
    close_app();
    sh.open = i;
    sh.app_scr = lv_obj_create(NULL);
    th_screen(sh.app_scr);
    lv_obj_remove_flag(sh.app_scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(sh.app_scr, th->body, 0);
    lv_obj_set_style_text_color(sh.app_scr, lv_color_hex(th->text), 0);
    lv_obj_add_event_cb(sh.app_scr, app_gesture, LV_EVENT_GESTURE, NULL);
    apps[i].open(sh.app_scr);
    if (TH_TERMINAL) { /* the hints say how back works */
        go(sh.app_scr, LV_SCREEN_LOAD_ANIM_NONE, 0, false);
        if (old) lv_obj_delete_async(old);
        return;
    }
    if (apps[i].vertical) { lv_screen_load_anim(sh.app_scr, LV_SCREEN_LOAD_ANIM_FADE_IN, 200, 0, old != NULL); return; } /* no pull-down */
    lv_obj_t *pill = lv_obj_create(sh.app_scr); /* "pull down to go back" handle */
    lv_obj_remove_style_all(pill);
    lv_obj_set_size(pill, 44, 5);
    lv_obj_set_style_radius(pill, 3, 0);
    lv_obj_set_style_bg_opa(pill, LV_OPA_50, 0);
    lv_obj_set_style_bg_color(pill, lv_color_hex(th->dim), 0);
    lv_obj_align(pill, LV_ALIGN_TOP_MID, 0, 14);
    lv_screen_load_anim(sh.app_scr, LV_SCREEN_LOAD_ANIM_FADE_IN, 200, 0, old != NULL); /* the replaced app goes after */
}

static void back_to_menu(void)
{
    if (sh.open < 0) return;
    const char *group = apps[sh.open].group;
    if (group && sh.style != MENU_LIST) { shell_open(group); return; } /* out of a game: back to the games' own screen */
    int i = sh.open;
    close_app();
    if (group) { /* or to the games in the list, on this one */
        int lo = i;
        while (lo > app_count && apps[lo - 1].group && !strcmp(apps[lo - 1].group, group)) lo--;
        group_enter(group, i - lo);
    }
    layout();
    go(sh.menu_scr, LV_SCREEN_LOAD_ANIM_MOVE_BOTTOM, 250, true); /* the app's screen goes when the slide is done */
}

/* ---------- navigation ---------- */

bool shell_open(const char *id)
{
    for (int i = 0; i < app_total; i++)
        if (!strcmp(apps[i].id, id)) {
            if (i < app_count) {
                if (sh.group) group_leave();
                sh.pos = (float)i, sh.pan_x = -sh.hx[i], sh.pan_y = -sh.hy[i];
            } /* a grouped one leaves the menu where it was */
            layout();
            open_app(i);
            return true;
        }
    return false;
}

void shell_set_theme(int theme)
{
    theme_select(theme);
    lv_obj_t *app = sh.open >= 0 ? sh.app_scr : NULL;
    close_app();
    restyle_menu();
    if (app) { /* picked inside the Style app: land in the restyled menu at once, then drop the app's screen.
                  (A fade plus a delayed delete raced: restyling blocks for a moment, both timers came due together and
                  the screen was freed while the fade still used it - LVGL then spun on a dead object.) */
        lv_screen_load(sh.menu_scr);
        lv_obj_delete_async(app);
    }
}

/* The panel is about to write new firmware: say so on top of everything. The chip then sits in the flasher and draws
   nothing, but the panel keeps its last frame, so this stays on the glass until the new firmware boots. */
void shell_updating(void)
{
    if (sh.updating) return;
    sh.updating = true;
    pip_wake();
    lv_obj_t *o = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, SCR, SCR);
    th_screen(o);
    lv_obj_add_flag(o, LV_OBJ_FLAG_CLICKABLE); /* swallows touches */
    th_text_at(o, th->title ? th->title : th->body, th->text, th_label("Обновление"), -1, 176);
    th_text_at(o, th->small, th->accent, th_label("Не отключай кабель"), -1, 252);
}

void shell_home(void)
{
    if (lv_screen_active() == sh.pip_scr) return;
    if (sh.group) group_leave(); /* the menu comes back as the menu */
    bool app = sh.open >= 0;
    close_app();
    go(sh.pip_scr, LV_SCREEN_LOAD_ANIM_MOVE_BOTTOM, 250, app); /* an app screen is deleted after */
}

/* Top (PWR): Pip -> menu -> the app in the middle; apps may use it. Bottom (BOOT): "meet" on Pip,
   back to Pip from the menu, back to the menu from an app unless the app uses it. */
void shell_button(int id)
{
    bool dark = !pip_display_on();
    pip_seen();
    if (dark) return; /* the press that wakes the screen does nothing else */
    if (phone_button(id)) return; /* a call, the pairing or a banner answers first */
    lv_obj_t *now = lv_screen_active();
    if (now == sh.pip_scr) {
        if (id == PIP_BTN_PWR) show_menu(LV_SCREEN_LOAD_ANIM_FADE_IN);
        else pip_meet();
    } else if (now == sh.menu_scr) {
        if (id == PIP_BTN_PWR) open_app(sh.style == MENU_HONEYCOMB ? hex_focus() : sh.style == MENU_LIST ? sh.lo + list_middle() : car_middle());
        else if (sh.group) group_leave(); /* out of the games first */
        else shell_home();
    } else if (sh.open >= 0) {
        if (!(apps[sh.open].button && apps[sh.open].button(id))) back_to_menu();
    }
}

static void from_pip(int dir)
{
    show_menu(sh.style == MENU_HONEYCOMB ? LV_SCREEN_LOAD_ANIM_FADE_IN
              : dir < 0                  ? LV_SCREEN_LOAD_ANIM_MOVE_LEFT
                                         : LV_SCREEN_LOAD_ANIM_MOVE_RIGHT);
}

void shell_time(int64_t unix_s, int tz_min)
{
    sh.now = unix_s;
    sh.tz = tz_min;
    pip_time(unix_s, tz_min);
}

void shell_button_up(int id)
{
    if (sh.open >= 0 && apps[sh.open].button_up) apps[sh.open].button_up(id);
}

void shell_gyro(float gx, float gy, float gz, float dt)
{
    if (sh.open >= 0 && apps[sh.open].gyro) apps[sh.open].gyro(gx, gy, gz, dt);
}

void shell_imu(float ax, float ay, float az, float dt)
{
    bool awake = sh.open >= 0 && apps[sh.open].awake;
    pip_hold_awake(awake || sh.updating);
    if (!pip_display_on() && lv_screen_active() != sh.pip_scr && !awake && !phone_where()) shell_home(); /* asleep: back to his face */
    pip_shake_ok(sh.open < 0 || !apps[sh.open].imu); /* shaking a die is not shaking him */
    pip_imu(ax, ay, az, dt);
    if (sh.open >= 0 && apps[sh.open].imu) apps[sh.open].imu(ax, ay, az, dt);
}

void shell_create(void)
{
    sh.pip_scr = lv_screen_active();
    pip_create(sh.pip_scr);
    pip_on_swipe(from_pip);

    sh.menu_scr = lv_obj_create(NULL);
    lv_obj_remove_flag(sh.menu_scr, LV_OBJ_FLAG_SCROLLABLE);
    for (int i = 0; i < ICON_COUNT; i++) {
        sh.sprites[i] = lv_draw_buf_create(ICON, ICON, LV_COLOR_FORMAT_ARGB8888, 0);
        LV_ASSERT_MALLOC(sh.sprites[i]);
    }
    /* honeycomb: the first app in the middle, the rest spiralling out ring by ring */
    static const int dirs[6][2] = {{1, 0}, {0, 1}, {-1, 1}, {-1, 0}, {0, -1}, {1, -1}}; /* axial hex steps */
    int n = 0, q = 0, r = 0;
    sh.hx[n] = sh.hy[n] = 0, n++;
    for (int ring = 1; n < app_count; ring++) {
        q = ring * dirs[4][0], r = ring * dirs[4][1];
        for (int side = 0; side < 6 && n < app_count; side++)
            for (int step = 0; step < ring && n < app_count; step++, n++) {
                sh.hx[n] = HEX_SPACING * (q + r * 0.5f), sh.hy[n] = HEX_SPACING * 0.866f * r;
                q += dirs[side][0], r += dirs[side][1];
            }
    }
    sh.min_x = sh.min_y = 1e9f, sh.max_x = sh.max_y = -1e9f;
    for (int i = 0; i < app_count; i++) {
        sh.min_x = fminf(sh.min_x, sh.hx[i]), sh.max_x = fmaxf(sh.max_x, sh.hx[i]);
        sh.min_y = fminf(sh.min_y, sh.hy[i]), sh.max_y = fmaxf(sh.max_y, sh.hy[i]);
    }
    sh.lo = 0, sh.n = app_count;
    for (int i = 0; i < app_total; i++) {
        if (i < app_count) {
            sh.icons[i] = lv_image_create(sh.menu_scr);
            lv_image_set_src(sh.icons[i], sh.sprites[apps[i].icon]);
            lv_image_set_pivot(sh.icons[i], ICON / 2, ICON / 2);
        }
        sh.rows[i] = lv_image_create(sh.menu_scr); /* a tinted alpha mask, see rows_build */
        lv_obj_set_style_image_recolor_opa(sh.rows[i], LV_OPA_COVER, 0);
    }
    sh.lens = lv_obj_create(sh.menu_scr); /* clips its own copy of the rows: whatever passes through reads big */
    lv_obj_remove_style_all(sh.lens);
    lv_obj_set_size(sh.lens, LENS_W, LENS_H);
    lv_obj_set_pos(sh.lens, LENS_X, (SCR - LENS_H) / 2);
    lv_obj_remove_flag(sh.lens, LV_OBJ_FLAG_CLICKABLE);
    for (int i = 0; i < app_total; i++) {
        sh.lens_rows[i] = lv_image_create(sh.lens);
        lv_obj_set_style_image_recolor_opa(sh.lens_rows[i], LV_OPA_COVER, 0);
    }
    sh.lens_cursor = th_cursor(sh.lens, 12, 28, 0);
    sh.chrome = lv_obj_create(sh.menu_scr);
    lv_obj_remove_style_all(sh.chrome);
    lv_obj_set_size(sh.chrome, SCR, SCR);
    lv_obj_remove_flag(sh.chrome, LV_OBJ_FLAG_CLICKABLE);
    sh.marker = lv_obj_create(sh.menu_scr); /* the dial's pointer on the left */
    lv_obj_remove_style_all(sh.marker);
    lv_obj_set_size(sh.marker, 14, 14);
    lv_obj_set_style_radius(sh.marker, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(sh.marker, LV_OPA_COVER, 0);
    lv_obj_align(sh.marker, LV_ALIGN_LEFT_MID, 62, 0);
    sh.name = lv_label_create(sh.menu_scr);
    sh.bat = lv_obj_create(sh.menu_scr); /* the body with the fill inside, the nub on the right */
    lv_obj_remove_style_all(sh.bat);
    lv_obj_set_size(sh.bat, 38, 16);
    lv_obj_remove_flag(sh.bat, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(sh.bat, LV_ALIGN_TOP_MID, 0, 14);
    lv_obj_t *body = lv_obj_create(sh.bat);
    lv_obj_remove_style_all(body);
    lv_obj_set_size(body, 34, 16);
    lv_obj_set_style_radius(body, 4, 0);
    lv_obj_set_style_border_width(body, 2, 0);
    lv_obj_set_style_border_color(body, lv_color_hex(0x8E8E93), 0);
    lv_obj_set_style_pad_all(body, 2, 0);
    sh.bat_fill = lv_obj_create(body);
    lv_obj_remove_style_all(sh.bat_fill);
    lv_obj_set_size(sh.bat_fill, 2, 8);
    lv_obj_set_style_radius(sh.bat_fill, 2, 0);
    lv_obj_set_style_bg_opa(sh.bat_fill, LV_OPA_COVER, 0);
    th_box(sh.bat, 35, 5, 3, 6, 1, 0x8E8E93);
    bat_show();
    lv_obj_add_flag(sh.menu_scr, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(sh.menu_scr, menu_input, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(sh.menu_scr, menu_input, LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(sh.menu_scr, menu_input, LV_EVENT_RELEASED, NULL);
    sh.glide = lv_timer_create(hex_glide, 16, NULL);
    restyle_menu();
    apps_init();
}
