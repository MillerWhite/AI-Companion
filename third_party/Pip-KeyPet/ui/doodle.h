/* Small procedural sprites for effects around the face, drawn once into ARGB8888 buffers. Pure C. */
#pragma once
#include <stdint.h>

void doodle_rain_cloud(uint32_t *px, int w, int h); /* dark rain cloud, about 170x80 */

/* App icons, about 150x150: a glass disc with a white symbol, a thin line drawing, or a mosaic of cells */
enum { ICON_STYLE_GLASS, ICON_STYLE_OUTLINE, ICON_STYLE_BLOCKS };
enum { ICON_CLOCK, ICON_TIMER, ICON_TORCH, ICON_QR, ICON_PIP, ICON_GAME, ICON_BELL, ICON_MUSIC, ICON_CAMERA,
       ICON_STOPWATCH, ICON_BADGE, ICON_THEME, ICON_NOISE, ICON_XO, ICON_EIGHT, ICON_COIN, ICON_DICE, ICON_MAZE, ICON_REMOTE, ICON_BT, ICON_COUNT };
void doodle_icon(int icon, int style, uint32_t color, uint32_t ink, uint32_t line, uint32_t *px, int w, int h);
void doodle_icon_mask(int icon, uint8_t *a8, int stride, int size); /* just the symbol as coverage, for a menu row */

/* Brand marks for the middle of a QR code, composited into an ARGB buffer (stride in pixels) */
enum { BRAND_NONE, BRAND_TELEGRAM, BRAND_INSTAGRAM };
void doodle_brand(int brand, uint32_t *px, int stride, int x0, int y0, int size);
void doodle_brand_ink(int brand, uint32_t *px, int stride, int x0, int y0, int size, uint32_t ink, uint32_t paper); /* one colour, no tile */
