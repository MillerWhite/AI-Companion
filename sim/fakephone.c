#include "fakephone.h"
#include "lvgl.h"
#include "phone.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static const struct { const char *app, *title, *text; int h, m; } notes[] = {
    {"Telegram", "Аня", "Встречаемся в 7 у входа? Возьми зарядку, у меня телефон на нуле", 14, 27},
    {"Календарь", "Созвон через 15 мин", "Дизайн-ревью, переговорка 3", 13, 0},
    {"Почта", "Счёт за октябрь", "Во вложении счёт и акт за октябрь", 14, 31},
    {"Telegram", "Чат «Студия» 🎬", "Миша: короче, план на завтра — в 10:00 созвон по проекту №4, потом “финальный” монтаж. "
     "Не забудь скинуть исходники 👍 и обложку, а то опять будем сидеть до ночи. Если что-то не успеваешь — пиши заранее, "
     "перенесём. Всем спасибо за сегодня ✌️", 15, 2},
};
static const struct { const char *title, *artist; int dur; } tracks[] = {
    {"Nightcall", "Kavinsky", 258}, {"Midnight City", "M83", 244}, {"Resonance", "Home", 213}, {"Everything In Its Right Place", "Radiohead", 251},
};

static struct {
    int link, note, track, shots;
    uint32_t uid;
    bool playing, music;
    float pos, vol;
    uint32_t at;
    const char *last;
    int host;                          /* the fake computer: HOST_* */
    int nexts, prevs, clicks, mx, my, moves, scroll, drags, held; /* what the remote sent it */
    const char *media;                 /* the last media key */
} fp = {.vol = 0.5f, .last = ""};

static float pos_now(void) { return fp.pos + (fp.playing ? lv_tick_elaps(fp.at) / 1000.0f : 0); }

static void send_track(void)
{
    if (!fp.music || fp.link != PHONE_ON) { phone_track(NULL, NULL, 0, 0, false); return; }
    phone_track(tracks[fp.track].title, tracks[fp.track].artist, tracks[fp.track].dur, fp.pos, fp.playing);
}

static void on_command(int cmd, uint32_t uid)
{
    fp.last = phone_command_name(cmd);
    int n = sizeof tracks / sizeof tracks[0];
    switch (cmd) {
    case PHONE_NOTE_DISMISS: phone_note_gone(uid); break;
    case PHONE_CALL_ANSWER:
    case PHONE_CALL_DECLINE: phone_call(0, NULL, NULL); break;
    case PHONE_PLAY_PAUSE: fp.pos = pos_now(), fp.at = lv_tick_get(), fp.playing = !fp.playing, send_track(); break;
    case PHONE_NEXT:
    case PHONE_PREV: fp.track = (fp.track + (cmd == PHONE_NEXT ? 1 : n - 1)) % n, fp.pos = 0, fp.at = lv_tick_get(), send_track(); break;
    case PHONE_VOL_UP:
    case PHONE_VOL_DOWN: fp.vol = fmaxf(0, fminf(1, fp.vol + (cmd == PHONE_VOL_UP ? 1 : -1) / 16.0f)), phone_volume(fp.vol); break;
    case PHONE_SHUTTER: fp.shots++; break;
    case PHONE_PAIR_OK:
        if (fp.host == HOST_PAIRING) { fp.host = HOST_ON, phone_host(HOST_ON, 0); break; } /* it was the computer asking */
        fp.link = PHONE_ON, phone_link(PHONE_ON, 0), phone_volume(fp.vol), send_track();
        break;
    case PHONE_PAIR_CANCEL:
        if (fp.host == HOST_PAIRING) { fp.host = HOST_OFF, phone_host(HOST_OFF, 0); break; }
        fp.link = PHONE_OFF, phone_link(PHONE_OFF, 0);
        break;
    }
}

static void on_host(int what, int dx, int dy)
{
    static const char *media[] = {"play_pause", "track_next", "track_prev", "vol_up", "vol_down"};
    if (what == HOST_NEXT) fp.nexts++;
    else if (what == HOST_PREV) fp.prevs++;
    else if (what == HOST_CLICK) fp.clicks++;
    else if (what == HOST_DOWN) fp.drags++, fp.held = 1;
    else if (what == HOST_UP) fp.held = 0;
    else if (what == HOST_MOVE) fp.mx += dx, fp.my += dy, fp.moves++;
    else if (what == HOST_SCROLL) fp.scroll += dy;
    else if (what >= HOST_PLAY_PAUSE && what <= HOST_VOL_DOWN) fp.media = media[what - HOST_PLAY_PAUSE];
}

static void bt_query(phone_bt_t *s) /* the Bluetooth page: the pretend phone and computer */
{
    s->n = 0, s->bonds = 2, s->advertising = true;
    if (fp.link == PHONE_ON) s->peer[s->n++] = (phone_peer_t){.role = PEER_PHONE, .rssi = -52, .secs = 754, .encrypted = true, .bonded = true, .ready = true, .ancs = true, .ams = true, .cts = true};
    if (fp.host == HOST_ON) s->peer[s->n++] = (phone_peer_t){.role = PEER_COMPUTER, .rssi = -61, .secs = 120, .encrypted = true, .bonded = true};
}
static void bt_act(int what) { fp.last = what == BT_RECONNECT_PHONE ? "bt reconnect" : "bt forget"; }

void fake_phone_init(void) { phone_on_command(on_command), phone_on_host(on_host), phone_on_bt(bt_query, bt_act); }

void fake_host(int arg) /* X0 gone, X1 a computer asks to pair (the code on screen), X2 a paired one is back */
{
    fp.host = arg == 1 ? HOST_PAIRING : arg == 2 ? HOST_ON : HOST_OFF;
    phone_host(fp.host, 135790);
}

const char *fake_host_report(void)
{
    static char s[200];
    snprintf(s, sizeof s, "host=%s nexts=%d prevs=%d clicks=%d mx=%d my=%d scroll=%d media=%s drags=%d held=%d",
             fp.host == HOST_ON ? "on" : fp.host == HOST_PAIRING ? "pairing" : "off", fp.nexts, fp.prevs, fp.clicks, fp.mx, fp.my,
             fp.scroll, fp.media ? fp.media : "-", fp.drags, fp.held);
    return s;
}
const char *fake_phone_last(void) { return fp.last; }
int fake_phone_shots(void) { return fp.shots; }

void fake_phone(char kind, int arg)
{
    if (kind == 'L') {
        fp.link = arg == 1 ? PHONE_PAIRING : arg == 2 ? PHONE_ON : PHONE_OFF;
        phone_link(fp.link, 482913);
        if (fp.link == PHONE_ON) phone_volume(fp.vol), send_track();
    } else if (fp.link != PHONE_ON) {
        return; /* a phone that is not connected tells us nothing */
    } else if (kind == 'N') {
        int k = fp.note++ % (int)(sizeof notes / sizeof notes[0]);
        phone_note(++fp.uid, notes[k].app, notes[k].title, notes[k].text, notes[k].h, notes[k].m, false);
    } else if (kind == 'C') {
        if (arg) phone_call(++fp.uid, "Мама", "мобильный");
        else phone_call(0, NULL, NULL);
    } else if (kind == 'M') {
        fp.music = arg != 0;
        fp.playing = fp.music, fp.pos = fp.music ? 102 : 0, fp.at = lv_tick_get();
        send_track();
    }
}
