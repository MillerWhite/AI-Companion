/* What the board glue in main.c shares with ble.c. */
#pragma once
#include <stdbool.h>

void ui_lock(void);   /* LVGL is not thread-safe: every touch of the UI from another task goes inside */
void ui_unlock(void);
extern int tz_min;    /* local time zone, minutes east of UTC; the iPhone sets it */

void ble_start(void); /* the iPhone link: ANCS notifications and calls, AMS music, CTS time */

void debug_start(void);
void settings_save(void);       /* power, features, brightness, privacy into NVS */
void settings_bright(int pct);  /* the panel's full brightness, 10..100 */
int settings_brightness(void);
void debug_line(char *line);              /* a command line from the BLE debug link (queued for the debug task) */
bool ble_debug_on(void);
void ble_debug_send(const char *s, int n); /* to the BLE debug page */
 /* the USB debug link: status out, commands in (tools/pipdebug.py) */
