/* The iPhone as the UI sees it: what the phone told us and what we ask it to do.
   The host fills it in (firmware: BLE with ANCS, AMS and a HID key; simulator: a fake phone) and carries out the commands.
   Nothing here needs an app on the phone. */
#pragma once
#include "lvgl.h"
#include <stdbool.h>
#include <stdint.h>

enum { PHONE_OFF, PHONE_PAIRING, PHONE_ON };
enum {
    PHONE_NOTE_DISMISS, /* ANCS negative action: clears it on the phone too */
    PHONE_CALL_ANSWER,  /* ANCS positive / negative on the incoming-call notification */
    PHONE_CALL_DECLINE,
    PHONE_PLAY_PAUSE,   /* AMS remote commands */
    PHONE_NEXT,
    PHONE_PREV,
    PHONE_VOL_UP,
    PHONE_VOL_DOWN,
    PHONE_SHUTTER,      /* HID consumer key "volume up": the Camera app's shutter */
    PHONE_PAIR_OK,      /* the passkey matches */
    PHONE_PAIR_CANCEL,
    PHONE_CMD_COUNT
};

/* ---------- host -> UI ---------- */
void phone_link(int state, uint32_t code);                                     /* PHONE_PAIRING: show the 6-digit code */
void phone_note(uint32_t uid, const char *app, const char *title, const char *text, int hour, int min, bool quiet); /* quiet: no banner */
void phone_note_gone(uint32_t uid);
void phone_call(uint32_t uid, const char *name, const char *kind);             /* name NULL: the call is over */
void phone_track(const char *title, const char *artist, int dur_s, float pos_s, bool playing); /* title NULL: nothing */
void phone_volume(float v);                                                     /* 0..1 */

/* ---------- UI -> host ---------- */
void phone_on_command(void (*cb)(int cmd, uint32_t uid));
const char *phone_command_name(int cmd);

/* ---------- for the shell and the app list ---------- */
bool phone_button(int id);      /* the pairing, call and banner overlays take the side buttons first */
const char *phone_where(void);  /* "pair", "call" or "banner" while one is up, else NULL */
void phone_notify_open(lv_obj_t *scr);
void phone_notify_close(void);
bool phone_notify_button(int id);
void phone_music_open(lv_obj_t *scr);
void phone_music_close(void);
bool phone_music_button(int id);
void phone_camera_open(lv_obj_t *scr);
void phone_camera_close(void);
bool phone_camera_button(int id);

/* ---------- a computer: Pip as a slide clicker and a pointer ----------
   The firmware is a BLE mouse to it (a second HID service; no keyboard, or an iPhone would hide its own): next slide =
   a click (Keynote, PowerPoint, Google Slides), previous = the wheel a notch up (PowerPoint, Google Slides), and the
   pointer follows the gyroscope while a finger is on the screen. */
enum { HOST_OFF, HOST_PAIRING, HOST_ON };
enum { HOST_NEXT, HOST_PREV, /* the page keys: the next slide, the one before */
       HOST_CLICK, HOST_MOVE,  /* the mouse: HOST_MOVE carries dx, dy in px */
       HOST_DOWN, HOST_UP,     /* the left button held (a drag: windows, files, a selection) and let go */
       HOST_SCROLL,            /* dy wheel notches, > 0: the page goes down */
       HOST_PLAY_PAUSE, HOST_TRACK_NEXT, HOST_TRACK_PREV, HOST_VOL_UP, HOST_VOL_DOWN }; /* media keys */
void phone_host(int state, uint32_t code);                 /* host -> UI: a computer came (HOST_PAIRING: the code), or went */
int phone_host_state(void);
void phone_on_host(void (*cb)(int what, int dx, int dy));  /* UI -> host */

/* What notifications show on a screen anyone can glance at. HIDE (the default): the app and the time; the sender and
   the text only once you open one. OPEN: everything. OFF: nothing is fetched from the phone at all (calls still ring).
   Codes, passwords and banks are never kept, in any mode. */
enum { PHONE_PRIV_OPEN, PHONE_PRIV_HIDE, PHONE_PRIV_OFF, PHONE_PRIV_COUNT };

/* The Bluetooth page: what the host knows about each link, and what it can do about them */
enum { PEER_PHONE, PEER_COMPUTER, PEER_DEBUG, PEER_UNKNOWN };
typedef struct {
    int role, rssi, secs;               /* rssi in dBm (0: unknown), how long it has been connected */
    bool encrypted, bonded, ready, ancs, ams, cts; /* ready..cts: the phone's services */
} phone_peer_t;
typedef struct { int n, bonds; bool advertising; phone_peer_t peer[5]; } phone_bt_t;
enum { BT_RECONNECT_PHONE, BT_FORGET_ALL };
void phone_on_bt(void (*query)(phone_bt_t *out), void (*act)(int what));
void phone_bt_open(lv_obj_t *scr);
void phone_bt_close(void);
void phone_set_privacy(int mode);
int phone_privacy(void);
void phone_on_privacy(void (*cb)(int mode)); /* the host keeps the choice */
void phone_remote_open(lv_obj_t *scr);
void phone_remote_close(void);
bool phone_remote_button(int id);
void phone_remote_button_up(int id); /* the bottom key let go: a short press turns back, a long one was scrolling */
void phone_remote_imu(float ax, float ay, float az, float dt);   /* g: which way is up, for the pointer */
void phone_remote_gyro(float gx, float gy, float gz, float dt); /* deg/s, device axes (see imu.h) */
