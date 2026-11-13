/* Navigation: Pip's face <-> app carousel <-> apps. Hosts call these instead of the pip_* ones. */
#pragma once
#include "lvgl.h"
#include <stdbool.h>
#include <stdint.h>


void shell_create(void);
void shell_time(int64_t unix_s, int tz_min);            /* wall clock for Pip and the apps */
void shell_imu(float ax, float ay, float az, float dt); /* accelerometer in g, device axes */
void shell_gyro(float gx, float gy, float gz, float dt); /* gyroscope in deg/s, the same axes: for the apps that want it */
void shell_button(int id);                              /* top PIP_BTN_PWR = menu / open, bottom PIP_BTN_BOOT = back / meet */
void shell_button_up(int id);                           /* a key let go (only the bottom one tells: the top is the PMU's) */
const char *shell_where(void);                          /* "pip", "menu" or the open app's id */
enum { MENU_HONEYCOMB, MENU_CAROUSEL, MENU_LIST };
void shell_set_menu_style(int style);                   /* honeycomb, the first carousel, or a vertical list */
void shell_set_theme(int theme);                        /* TH_* from theme.h: restyles the menu and the apps */
int shell_menu_style(void);
bool shell_open(const char *id);                        /* jump straight into an app */

/* ---------- for the apps ---------- */
typedef struct {
    const char *id, *name;
    int icon;       /* ICON_* from doodle.h */
    uint32_t color; /* icon disc */
    void (*open)(lv_obj_t *scr);
    void (*close)(void);                                  /* may be NULL */
    void (*imu)(float ax, float ay, float az, float dt); /* may be NULL */
    void (*gyro)(float gx, float gy, float gz, float dt); /* may be NULL */
    bool (*button)(int id);                               /* may be NULL; true = the app used the side button */
    void (*button_up)(int id);                            /* may be NULL; the key let go (for holding it) */
    bool vertical;                                        /* the app uses vertical drags itself: no swipe-down back */
    bool awake;                                           /* there to be looked at: no falling asleep while it is open */
    const char *group;                                    /* NULL: in the menu; else the id of the app that lists it (back goes there) */
} app_t;

extern const app_t apps[];
extern const int app_count; /* the menu's: the first ones; the grouped ones follow up to app_total */
extern const int app_total;
void apps_init(void);                  /* background work that outlives an open app (the timer) */
void shell_home(void);                 /* back to Pip */
void shell_updating(void);             /* "Обновление" over everything, held lit: the panel is about to flash */
int64_t shell_local_time(void);        /* seconds, already shifted to the local zone: feed it to gmtime_r */
void shell_battery(int percent, bool charging); /* from the power chip; -1 = unknown */
int shell_battery_level(bool *charging);        /* -1 = unknown */
const char *shell_battery_text(void);           /* "82%", "82%+" while charging, "--" */
lv_draw_buf_t *shell_icon(int icon);   /* the carousel's icon sprite, to reuse inside an app */
