#include "theme.h"
#include "shell.h"
#include <limits.h>
#include <math.h>
#include <string.h>

#define SCR 466

LV_FONT_DECLARE(font_silence_20) LV_FONT_DECLARE(font_silence_28) LV_FONT_DECLARE(font_silence_96)
LV_FONT_DECLARE(font_instr_20) LV_FONT_DECLARE(font_instr_28) LV_FONT_DECLARE(font_instr_96) LV_FONT_DECLARE(font_instr_56) LV_FONT_DECLARE(font_instr_40)
LV_FONT_DECLARE(font_apple_20) LV_FONT_DECLARE(font_apple_28) LV_FONT_DECLARE(font_apple_96)
LV_FONT_DECLARE(font_diag_16) LV_FONT_DECLARE(font_diag_16b) LV_FONT_DECLARE(font_diag_20) LV_FONT_DECLARE(font_diag_20b)
LV_FONT_DECLARE(font_diag_28)
LV_FONT_DECLARE(font_diag_96) LV_FONT_DECLARE(font_diag_56) LV_FONT_DECLARE(font_diag_40)

const theme_t themes[TH_COUNT] = {
    [TH_SILENCE] = {.name = "Тишина", .bg = 0x000000, .text = 0xF2F2F2, .dim = 0x8C8C8C, .faint = 0x262626, .panel = 0x1A1A1A,
                    .accent = 0xFF3B30, .good = 0xF2F2F2, .small = &font_silence_20, .tiny = &font_silence_20, .tiny_bold = &font_silence_20, .body = &font_silence_28,
                    .digits = &font_silence_96, .caps = true, .radius = LV_RADIUS_CIRCLE, .icons = ICONS_OUTLINE,
                    .clock = CLOCK_DOTS, .meter = METER_DOTS, .menu = MENU_HONEYCOMB,
                    .hidden = true /* the honeycomb lags on the board; back when it is fast */},
    [TH_INSTRUMENT] = {.name = "Приборка", .bg = 0x000000, .text = 0xE8E6E1, .dim = 0xA3A09A, .faint = 0x2A2A2A, .panel = 0x1C1C1C,
                       .accent = 0xFF5A1F, .good = 0xE8E6E1, .small = &font_instr_20, .tiny = &font_instr_20, .tiny_bold = &font_instr_20, .body = &font_instr_28,
                       .digits = &font_instr_96, .big = &font_instr_56, .title = &font_instr_40, .caps = true, .radius = 6, .icons = ICONS_OUTLINE, .clock = CLOCK_DIAL,
                       .meter = METER_SEGMENTS, .menu = MENU_LIST},
    [TH_APPLE] = {.name = "Apple", .bg = 0x000000, .text = 0xFFFFFF, .dim = 0x8E8E93, .faint = 0x3A3A3C, .panel = 0x1C1C1E,
                  .accent = 0x0091FF, .good = 0x30D158, .small = &font_apple_20, .tiny = &font_apple_20, .tiny_bold = &font_apple_20, .body = &font_apple_28, .digits = &font_apple_96,
                  .caps = false, .radius = LV_RADIUS_CIRCLE, .icons = ICONS_GLASS, .clock = CLOCK_BIG, .meter = METER_BAR,
                  .menu = MENU_HONEYCOMB, .hidden = true},
    [TH_DIAG] = {.name = "Диагностика", .bg = 0x3A1F1B, .text = 0xE6D8C3, .dim = 0xB9A592, .faint = 0x5A3530, .panel = 0x4A2A26,
                 .accent = 0xE6D8C3, .good = 0xE6D8C3, .grid = 0x5A3530, .mute = 0x8A6A60, .off = 0x4F2B26, .small = &font_diag_20, .body = &font_diag_28,
                 .digits = &font_diag_96, .big = &font_diag_56, .title = &font_diag_40, .tiny = &font_diag_16, .tiny_bold = &font_diag_16b, .button = &font_diag_20b, .caps = true, .radius = 0, .icons = ICONS_BLOCKS, .clock = CLOCK_BLOCKS,
                 .meter = METER_BLOCKS, .menu = MENU_LIST},
};

const theme_t *th = &themes[TH_INSTRUMENT];
void theme_select(int i) { th = &themes[i >= 0 && i < TH_COUNT ? i : TH_INSTRUMENT]; }
uint32_t th_mute(void) { return th->mute ? th->mute : th->dim; } /* the terminal's extra inks, or the nearest */
uint32_t th_off(void) { return th->off ? th->off : th->faint; }
int theme_index(void) { return (int)(th - themes); }

/* UTF-8 upper case for Latin and Russian, which is all the UI writes */
static void caps(char *out, const char *text, int max) /* every case pair is the same length, so it works in place too */
{
    const unsigned char *s = (const unsigned char *)text;
    int n = 0;
    while (*s && n < max) {
        if (*s >= 'a' && *s <= 'z') { out[n++] = (char)(*s++ - 32); continue; }
        if (s[0] == 0xD0 && s[1] >= 0xB0 && s[1] <= 0xBF) { out[n++] = (char)0xD0, out[n++] = (char)(s[1] - 0x20), s += 2; continue; } /* а..п */
        if (s[0] == 0xD1 && s[1] >= 0x80 && s[1] <= 0x8F) { out[n++] = (char)0xD0, out[n++] = (char)(s[1] + 0x20), s += 2; continue; } /* р..я */
        if (s[0] == 0xD1 && s[1] == 0x91) { out[n++] = (char)0xD0, out[n++] = (char)0x81, s += 2; continue; }                   /* ё */
        out[n++] = (char)*s++;
    }
    out[n] = 0;
}

const char *th_label(const char *text)
{
    if (!th->caps) return text;
    static char ring[4][96];
    static int slot;
    char *out = ring[slot = (slot + 1) % 4];
    caps(out, text, 92);
    return out;
}

lv_obj_t *th_text(lv_obj_t *parent, const lv_font_t *font, uint32_t color, const char *text)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    if (th->caps) lv_obj_set_style_text_letter_space(l, 1, 0);
    if (th->caps && strlen(text) > 92) { /* a long one: upper-case the label's own copy */
        lv_label_set_text(l, text);
        caps(lv_label_get_text(l), lv_label_get_text(l), INT_MAX);
        lv_label_set_text(l, NULL);
    } else lv_label_set_text(l, th_label(text));
    return l;
}

/* primary: filled with the accent (Apple: a tinted glass capsule); secondary: outlined */
lv_obj_t *th_button(lv_obj_t *parent, const char *text, bool primary, lv_event_cb_t cb)
{
    lv_obj_t *b = lv_button_create(parent);
    lv_obj_set_style_radius(b, th->radius, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_style_pad_hor(b, 20, 0);
    lv_obj_set_style_pad_ver(b, 12, 0);
    lv_obj_set_style_min_height(b, 52, 0);
    uint32_t fill = th == &themes[TH_APPLE] ? (primary ? th->good : th->panel) : primary ? th->accent : th->bg;
    lv_obj_set_style_bg_color(b, lv_color_hex(fill), 0);
    lv_obj_set_style_bg_opa(b, primary || th == &themes[TH_APPLE] ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    if (!primary && th != &themes[TH_APPLE]) {
        lv_obj_set_style_border_width(b, 2, 0);
        lv_obj_set_style_border_color(b, lv_color_hex(th->faint), 0);
    }
    if (th == &themes[TH_APPLE]) { /* a little glass: a lighter rim on top */
        lv_obj_set_style_border_width(b, 1, 0);
        lv_obj_set_style_border_side(b, LV_BORDER_SIDE_TOP, 0);
        lv_obj_set_style_border_color(b, lv_color_white(), 0);
        lv_obj_set_style_border_opa(b, LV_OPA_40, 0);
    }
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, NULL);
    uint32_t ink = primary && th != &themes[TH_APPLE] ? th->bg : th->text;
    lv_obj_t *l = th_text(b, th->button ? th->button : th->small, ink, text);
    lv_obj_center(l);
    return b;
}

/* the dot grid of "Диагностика", rendered once into a full-screen background: one blit per redraw instead of a
   tiled 11x11 image (some 1700 small blits for the whole screen, a fifth of a menu frame on the board) */
static lv_draw_buf_t *grid_bg;
static uint32_t grid_for = 0xFFFFFFFF;
void th_screen(lv_obj_t *scr)
{
    lv_obj_set_style_bg_color(scr, lv_color_hex(th->bg), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    if (!th->grid) { lv_obj_set_style_bg_image_src(scr, NULL, 0); return; }
    if (!grid_bg) grid_bg = lv_draw_buf_create(SCR, SCR, LV_COLOR_FORMAT_RGB565, 0);
    if (grid_for != (th->bg ^ th->grid << 1)) { /* redrawn only when the colours change */
        grid_for = th->bg ^ th->grid << 1;
        uint16_t bg = lv_color_to_u16(lv_color_hex(th->bg)), dot = lv_color_to_u16(lv_color_hex(th->grid));
        uint16_t *px = (uint16_t *)grid_bg->data;
        uint32_t stride = grid_bg->header.stride / 2;
        for (int y = 0; y < SCR; y++)
            for (int x = 0; x < SCR; x++) /* a 2x2 dot in the gap between cells, which start at 9 + 11k */
                px[y * stride + x] = (x % 11 == 7 || x % 11 == 8) && (y % 11 == 7 || y % 11 == 8) ? dot : bg;
    }
    lv_obj_set_style_bg_image_src(scr, grid_bg, 0);
    lv_obj_set_style_bg_image_tiled(scr, false, 0);
}

/* ---------- the terminal kit ---------- */

typedef struct { th_cell_t *v; int n; } cellmap_t;

static void cells_draw(lv_event_t *e)
{
    lv_obj_t *o = lv_event_get_target_obj(e);
    cellmap_t *m = lv_event_get_user_data(e);
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_area_t base, clip = layer->_clip_area;
    lv_obj_get_coords(o, &base);
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    for (int i = 0; i < m->n; i++) {
        const th_cell_t *c = &m->v[i];
        lv_area_t a = {base.x1 + c->x, base.y1 + c->y, base.x1 + c->x + c->w - 1, base.y1 + c->y + c->h - 1};
        if (c->color == TH_NONE || a.x2 < clip.x1 || a.x1 > clip.x2 || a.y2 < clip.y1 || a.y1 > clip.y2) continue;
        d.bg_color = lv_color_hex(c->color);
        lv_draw_rect(layer, &d, &a);
    }
}

static void cells_free(lv_event_t *e) { lv_free(lv_event_get_user_data(e)); }

lv_obj_t *th_cells(lv_obj_t *parent, th_cell_t *cells, int n)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, lv_pct(100), lv_pct(100));
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
    cellmap_t *m = lv_malloc(sizeof *m);
    LV_ASSERT_MALLOC(m);
    m->v = cells, m->n = n;
    lv_obj_add_event_cb(o, cells_draw, LV_EVENT_DRAW_MAIN, m);
    lv_obj_add_event_cb(o, cells_free, LV_EVENT_DELETE, m);
    return o;
}

void th_cell_set(lv_obj_t *o, th_cell_t *c, uint32_t color)
{
    if (c->color == color) return;
    c->color = color;
    lv_area_t base;
    lv_obj_get_coords(o, &base);
    lv_area_t a = {base.x1 + c->x, base.y1 + c->y, base.x1 + c->x + c->w - 1, base.y1 + c->y + c->h - 1};
    lv_obj_invalidate_area(o, &a);
}

static void blink_exec(void *o, int32_t v) { lv_obj_set_style_opa(o, v & 1 ? LV_OPA_TRANSP : LV_OPA_COVER, 0); }

lv_obj_t *th_cursor(lv_obj_t *parent, int w, int h, uint32_t color)
{
    lv_obj_t *c = lv_obj_create(parent);
    lv_obj_remove_style_all(c);
    lv_obj_set_size(c, w, h);
    lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(c, lv_color_hex(color), 0);
    lv_obj_remove_flag(c, LV_OBJ_FLAG_CLICKABLE);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, c);
    lv_anim_set_exec_cb(&a, blink_exec);
    lv_anim_set_values(&a, 0, 2); /* 0 shown, 1 hidden, each half the period */
    lv_anim_set_duration(&a, 1000);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&a);
    return c;
}

lv_obj_t *th_header(lv_obj_t *scr, const char *text, int y)
{
    lv_obj_t *row = lv_obj_create(scr);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 4, 0);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(row, LV_ALIGN_TOP_MID, 0, y);
    static const char prefix[] = "пип-01 // "; /* the terminal's device tag; the other themes just say the section */
    if (!TH_TERMINAL && !strncmp(text, prefix, sizeof prefix - 1)) text += sizeof prefix - 1;
    lv_obj_t *l = th_text(row, th->tiny_bold, th->text, text);
    lv_obj_set_style_text_letter_space(l, TH_TERMINAL ? 2 : th->caps, 0);
    if (!TH_TERMINAL) return l; /* the cursor and the rule are the terminal's */
    th_cursor(row, 9, 16, th->text);
    lv_obj_t *rule = lv_obj_create(scr);
    lv_obj_remove_style_all(rule);
    lv_obj_set_size(rule, 176, 2);
    lv_obj_set_style_bg_opa(rule, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(rule, lv_color_hex(th->text), 0);
    lv_obj_set_pos(rule, 145, y + 26);
    return l;
}

lv_obj_t *th_footer(lv_obj_t *scr, const char *text, int y)
{
    lv_obj_t *l = th_text(scr, th->tiny, th->dim, text);
    lv_obj_align(l, LV_ALIGN_TOP_MID, 0, y);
    return l;
}

static int utf8_len(const char *s)
{
    int n = 0;
    for (; *s; s++) n += (*s & 0xC0) != 0x80;
    return n;
}

void th_leader(lv_obj_t *label, const char *key, const char *val, int width)
{
    const lv_font_t *f = lv_obj_get_style_text_font(label, 0);
    int adv = lv_font_get_glyph_width(f, '.', 0) + lv_obj_get_style_text_letter_space(label, 0);
    char k[48], dots[48], out[160];
    lv_snprintf(k, sizeof k, "%s", th_label(key));
    int n = width / adv - utf8_len(k) - utf8_len(val) - 2;
    n = n < 1 ? 1 : n > 40 ? 40 : n;
    memset(dots, TH_TERMINAL ? '.' : ' ', (size_t)n); /* dot leaders are the terminal's */
    dots[n] = 0;
    lv_snprintf(out, sizeof out, "%s #%06X %s# %s", k, (unsigned)th_mute(), dots, val);
    lv_label_set_recolor(label, true);
    lv_label_set_text(label, out);
}

static lv_obj_t *curtain;
static void curtain_exec(void *o, int32_t v) { lv_obj_set_y(o, v); }
static void curtain_done(lv_anim_t *a) { lv_obj_delete(a->var); curtain = NULL; }

void th_wipe(lv_obj_t *scr, uint32_t bg)
{
    if (curtain) lv_obj_delete(curtain); /* its animation goes with it */
    lv_screen_load(scr);
    /* a curtain the new screen's colour, lifted downward: its top edge is the scan line */
    curtain = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(curtain);
    lv_obj_set_size(curtain, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_style_bg_opa(curtain, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(curtain, lv_color_hex(bg), 0);
    lv_obj_set_style_border_side(curtain, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_border_width(curtain, 2, 0);
    lv_obj_set_style_border_color(curtain, lv_color_hex(th->text), 0);
    lv_obj_remove_flag(curtain, LV_OBJ_FLAG_CLICKABLE);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, curtain);
    lv_anim_set_exec_cb(&a, curtain_exec);
    lv_anim_set_values(&a, 0, LV_VER_RES);
    lv_anim_set_duration(&a, 220);
    lv_anim_set_completed_cb(&a, curtain_done);
    lv_anim_start(&a);
}

lv_obj_t *th_box(lv_obj_t *parent, int x, int y, int w, int h, int radius, uint32_t color)
{
    lv_obj_t *d = lv_obj_create(parent);
    lv_obj_remove_style_all(d);
    lv_obj_set_size(d, w, h);
    lv_obj_set_style_radius(d, radius, 0);
    lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(d, lv_color_hex(color), 0);
    lv_obj_set_pos(d, x, y);
    lv_obj_remove_flag(d, LV_OBJ_FLAG_CLICKABLE);
    return d;
}

/* 5x7 digits for the dot-matrix and block faces */
const char *const th_glyph[10][7] = {
    {"01110", "10001", "10011", "10101", "11001", "10001", "01110"}, {"00100", "01100", "00100", "00100", "00100", "00100", "01110"},
    {"01110", "10001", "00001", "00010", "00100", "01000", "11111"}, {"11110", "00001", "00001", "01110", "00001", "00001", "11110"},
    {"00010", "00110", "01010", "10010", "11111", "00010", "00010"}, {"11111", "10000", "11110", "00001", "00001", "10001", "01110"},
    {"00110", "01000", "10000", "11110", "10001", "10001", "01110"}, {"11111", "00001", "00010", "00100", "01000", "01000", "01000"},
    {"01110", "10001", "10001", "01110", "10001", "10001", "01110"}, {"01110", "10001", "10001", "01111", "00001", "00010", "01100"},
};
/* ---------- the terminal look (Диагностика): everything is cells, 9 px on an 11 px pitch ---------- */

/* block digits for a shape like "00:00": 5x7 cells a digit, a one-cell colon, one cell between; returns the count */
int th_blocks(th_cell_t *c, int x0, int y0, const char *shape, int pitch, int size)
{
    int n = 0, col = 0;
    for (; *shape; shape++) {
        int w = *shape == ':' ? 1 : 5;
        for (int r = 0; r < 7; r++)
            for (int k = 0; k < w; k++) c[n++] = (th_cell_t){(int16_t)(x0 + (col + k) * pitch), (int16_t)(y0 + r * pitch), (uint8_t)size, (uint8_t)size, TH_NONE};
        col += w + 1;
    }
    return n;
}

void th_blocks_show(lv_obj_t *o, th_cell_t *c, const char *s, uint32_t on)
{
    for (int n = 0; *s; s++)
        for (int r = 0; r < 7; r++) {
            if (*s == ':') { th_cell_set(o, &c[n++], r == 2 || r == 4 ? on : th->off); continue; }
            for (int k = 0; k < 5; k++) th_cell_set(o, &c[n++], th_glyph[*s - '0'][r][k] == '1' ? on : th->off);
        }
}

void th_blocks_hide(lv_obj_t *o, th_cell_t *c, int n)
{
    for (int i = 0; i < n; i++) th_cell_set(o, &c[i], TH_NONE);
}

/* n square cells around the screen centre, the first at 12 o'clock, going clockwise */
void th_ring(th_cell_t *c, int n, float r, int size)
{
    for (int i = 0; i < n; i++) {
        float a = i * 6.2831853f / n - 1.5707963f;
        c[i] = (th_cell_t){(int16_t)lroundf(SCR / 2 + cosf(a) * r - size / 2.0f), (int16_t)lroundf(SCR / 2 + sinf(a) * r - size / 2.0f),
                           (uint8_t)size, (uint8_t)size, th->off};
    }
}

/* a little picture in cells, rows of '1' */
int th_pattern(th_cell_t *c, const char *const *rows, int nrows, int x0, int y0, int pitch, int size, uint32_t color)
{
    int n = 0;
    for (int r = 0; r < nrows; r++)
        for (int k = 0; rows[r][k]; k++)
            if (rows[r][k] == '1') c[n++] = (th_cell_t){(int16_t)(x0 + k * pitch), (int16_t)(y0 + r * pitch), (uint8_t)size, (uint8_t)size, color};
    return n;
}

lv_obj_t *th_text_at(lv_obj_t *scr, const lv_font_t *font, uint32_t color, const char *text, int x, int y)
{
    lv_obj_t *l = th_text(scr, font, color, text);
    if (x < 0) lv_obj_align(l, LV_ALIGN_TOP_MID, 0, y);
    else lv_obj_set_pos(l, x, y), lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_LEFT, 0);
    return l;
}

lv_obj_t *th_button_at(lv_obj_t *scr, int x, int y, int w, const char *text, bool primary, lv_event_cb_t cb)
{
    lv_obj_t *b = th_button(scr, text, primary, cb);
    lv_obj_set_style_min_height(b, 0, 0);
    lv_obj_set_style_pad_all(b, 0, 0);
    lv_obj_set_size(b, w, 44);
    lv_obj_set_pos(b, x, y);
    if (!primary) lv_obj_set_style_border_color(b, lv_color_hex(th->text), 0);
    return b;
}

/* the inverted block: ink plate, text in the background colour, the cursor after it */
lv_obj_t *th_plate(lv_obj_t *scr, int x, int y, int w, int h, const lv_font_t *font, const char *text, bool left, lv_obj_t **label)
{
    if (!TH_TERMINAL) { /* elsewhere a quiet panel with the text in ink */
        lv_obj_t *p = th_box(scr, x, y, w, h, th->radius, th->panel);
        lv_obj_t *l = th_text(p, font, th->text, text);
        if (left) lv_obj_align(l, LV_ALIGN_LEFT_MID, 16, 0);
        else lv_obj_center(l);
        if (label) *label = l;
        return p;
    }
    lv_obj_t *p = th_box(scr, x, y, w, h, 0, th->text);
    lv_obj_set_flex_flow(p, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(p, left ? LV_FLEX_ALIGN_START : LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_left(p, left ? 16 : 0, 0);
    lv_obj_set_style_pad_column(p, 6, 0);
    lv_obj_t *l = th_text(p, font, th->bg, text);
    if (label) *label = l;
    th_cursor(p, 12, font == th->body ? 22 : 16, th->bg);
    return p;
}

