/* Visual themes for the menu and the apps (Pip's face stays as it is). */
#pragma once
#include "lvgl.h"
#include <stdbool.h>
#include <stdint.h>

enum { TH_SILENCE, TH_INSTRUMENT, TH_APPLE, TH_DIAG, TH_COUNT };
enum { ICONS_OUTLINE, ICONS_GLASS, ICONS_BLOCKS };      /* how app icons are drawn */
enum { CLOCK_DOTS, CLOCK_DIAL, CLOCK_BIG, CLOCK_BLOCKS }; /* the clock app's face */
enum { METER_DOTS, METER_SEGMENTS, METER_BAR, METER_BLOCKS };

typedef struct {
    const char *name;
    uint32_t bg, text, dim, faint, panel, accent, good, grid; /* grid: dot pattern colour, 0 = none */
    uint32_t mute, off;                       /* terminal only: third ink, an unlit cell */
    const lv_font_t *small, *body, *digits, *big, *title; /* capitals only, may be NULL: big for the badge, title for a track */
    const lv_font_t *tiny, *tiny_bold, *button; /* terminal only: 16 px data rows and headers, 20 px semibold buttons */
    bool caps;   /* labels in capitals */
    int radius;  /* buttons and cards; LV_RADIUS_CIRCLE for pills */
    int icons, clock, meter, menu; /* menu: MENU_* from shell.h */
    bool hidden;                   /* not offered in the Style app for now */
} theme_t;

extern const theme_t themes[TH_COUNT];
extern const theme_t *th; /* the current one */
void theme_select(int i);
int theme_index(void);
uint32_t th_mute(void);
uint32_t th_off(void);

/* helpers the apps share */
const char *th_label(const char *text);              /* upper-cased when the theme writes in capitals */
lv_obj_t *th_text(lv_obj_t *parent, const lv_font_t *font, uint32_t color, const char *text);
lv_obj_t *th_button(lv_obj_t *parent, const char *text, bool primary, lv_event_cb_t cb);
void th_screen(lv_obj_t *scr);                        /* background colour and the dot grid */

/* ---------- "Диагностика": the terminal look, built from cells on an 11 px pitch (see the design canvas) ---------- */
#define TH_TERMINAL (th == &themes[TH_DIAG])
#define TH_UP "\xE2\x96\xB2" /* triangles for the hardware button hints, in the terminal fonts only */
#define TH_DOWN "\xE2\x96\xBC"
#define TH_LEFT "\xE2\x97\x80"
#define TH_RIGHT "\xE2\x96\xB6"

#define TH_NONE 0xFF000000u /* a cell colour that draws nothing */
typedef struct { int16_t x, y; uint8_t w, h; uint32_t color; } th_cell_t;
lv_obj_t *th_cells(lv_obj_t *parent, th_cell_t *cells, int n); /* one object draws them all; the array outlives it */
void th_cell_set(lv_obj_t *o, th_cell_t *c, uint32_t color);   /* recolour one, redraw just it */
lv_obj_t *th_cursor(lv_obj_t *parent, int w, int h, uint32_t color); /* a blinking block, 500/500 ms */
lv_obj_t *th_header(lv_obj_t *scr, const char *text, int y);       /* "ПИП-01 // ..." with the cursor and a rule; returns the label */
lv_obj_t *th_footer(lv_obj_t *scr, const char *text, int y);       /* the button hints */
void th_leader(lv_obj_t *label, const char *key, const char *val, int width); /* KEY ...... value, monospace */
void th_wipe(lv_obj_t *scr, uint32_t bg);                            /* load a screen with a top-down scan, 220 ms */
lv_obj_t *th_box(lv_obj_t *parent, int x, int y, int w, int h, int radius, uint32_t color); /* a plain filled rectangle */
extern const char *const th_glyph[10][7];                     /* 5x7 digits, rows of '1' */
int th_blocks(th_cell_t *c, int x0, int y0, const char *shape, int pitch, int size); /* cells for "00:00"-like digits */
void th_blocks_show(lv_obj_t *o, th_cell_t *c, const char *s, uint32_t on);
void th_blocks_hide(lv_obj_t *o, th_cell_t *c, int n);
void th_ring(th_cell_t *c, int n, float r, int size);         /* n cells round the centre, clockwise from 12 */
int th_pattern(th_cell_t *c, const char *const *rows, int nrows, int x0, int y0, int pitch, int size, uint32_t color);
lv_obj_t *th_text_at(lv_obj_t *scr, const lv_font_t *font, uint32_t color, const char *text, int x, int y); /* x < 0: centred */
lv_obj_t *th_button_at(lv_obj_t *scr, int x, int y, int w, const char *text, bool primary, lv_event_cb_t cb); /* 44 px high */
lv_obj_t *th_plate(lv_obj_t *scr, int x, int y, int w, int h, const lv_font_t *font, const char *text, bool left, lv_obj_t **label);
