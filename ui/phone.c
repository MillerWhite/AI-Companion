/* The iPhone apps and overlays, laid out after the "Диагностика" canvas boards (iPhone row).
   The model is filled by the host through phone.h; commands go back through phone_on_command(). */
#include "phone.h"
#include "doodle.h"
#include "pip.h"
#include "shell.h"
#include "theme.h"
#include "voice.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SCR 466
#define MAX_NOTES 16
#define ROW_H 87 /* notification list: px between rows */

typedef struct {
    uint32_t uid;
    char app[24], title[48], text[480]; /* what ANCS sends at most */
    uint8_t hour, min;
    bool unread, secret; /* secret: a code, a password or a bank: only the app is kept */
} note_t;

static struct {
    int link;
    uint32_t code;
    note_t *notes; /* newest first; on the heap: long texts, and the board puts big blocks in PSRAM */
    int n;
    struct { bool on; uint32_t uid; char name[48], kind[24]; } call;
    struct { bool has, playing; char title[64], artist[48]; int dur; float pos; uint32_t at; } tr;
    float vol, vol_phone; /* vol: shown; vol_phone: what the phone last said */
    void (*cb)(int cmd, uint32_t uid);
    /* overlays on the top layer */
    lv_obj_t *banner, *call_o, *pair_o, *call_grid, *call_arc;
    lv_timer_t *banner_t, *call_t;
    th_cell_t call_cells[60];
    uint32_t banner_uid, open_uid;
    int call_phase;
    const char *app; /* the phone app on screen, NULL if none */
    int host;        /* HOST_*: a computer, for the remote */
    bool pair_host;  /* the pairing overlay is the computer's */
    bool mu_phone;   /* the music app drives the iPhone though a computer is here too (a tap switches) */
    void (*host_cb)(int what, int dx, int dy);
} ph = {.vol = 0.5f};

static void send(int cmd, uint32_t uid)
{
    if (ph.cb) ph.cb(cmd, uid);
}

void phone_on_command(void (*cb)(int, uint32_t)) { ph.cb = cb; }

const char *phone_command_name(int cmd)
{
    static const char *const names[PHONE_CMD_COUNT] = {"note_dismiss", "call_answer", "call_decline", "play_pause", "next",
                                                       "prev", "vol_up", "vol_down", "shutter", "pair_ok", "pair_cancel"};
    return cmd >= 0 && cmd < PHONE_CMD_COUNT ? names[cmd] : "";
}

/* drop what the theme's fonts cannot draw (emoji, their joiners and variation selectors, rare symbols):
   a missing glyph would show as a gap or a box */
static void drawable_only(char *s)
{
    const lv_font_t *font = th->small;
    char *out = s;
    for (const unsigned char *p = (const unsigned char *)s; *p;) {
        uint32_t cp = *p;
        int n = cp < 0x80 ? 1 : cp >= 0xF0 ? 4 : cp >= 0xE0 ? 3 : cp >= 0xC0 ? 2 : 1;
        if (n > 1) {
            cp &= 0xFF >> (n + 1);
            for (int k = 1; k < n && p[k]; k++) cp = cp << 6 | (p[k] & 0x3F);
        }
        lv_font_glyph_dsc_t g;
        bool keep = cp == '\n' || (cp != 0x200D && !(cp >= 0xFE00 && cp <= 0xFE0F) && lv_font_get_glyph_dsc(font, &g, cp, 0));
        if (cp == ' ' && (out == s || out[-1] == ' ')) keep = false; /* the gap a dropped emoji leaves */
        if (keep) memmove(out, p, (size_t)n), out += n;
        p += n;
    }
    while (out > s && out[-1] == ' ') out--;
    *out = 0;
}

/* copy UTF-8 without cutting a character in half */
static void copy(char *d, size_t n, const char *s)
{
    size_t k = 0;
    while (s && s[k] && k + 1 < n) k++;
    while (k > 0 && s[k] && ((unsigned char)s[k] & 0xC0) == 0x80) k--; /* stopped inside a character: drop it */
    memcpy(d, s ? s : "", k);
    d[k] = 0;
}

/* "▲ word" in the terminal fonts, just the word elsewhere (the other themes have no triangles) */
static const char *hint(const char *glyph, const char *word)
{
    static char ring[4][64];
    static int slot;
    char *b = ring[slot = (slot + 1) % 4];
    snprintf(b, sizeof ring[0], "%s%s%s", TH_TERMINAL ? glyph : "", TH_TERMINAL ? " " : "", word);
    return b;
}

static lv_obj_t *footer(lv_obj_t *parent, const char *text, int y) { return TH_TERMINAL ? th_footer(parent, text, y) : NULL; }

static lv_obj_t *layer(void) /* a full-screen overlay that swallows touches */
{
    lv_obj_t *o = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, SCR, SCR);
    th_screen(o);
    lv_obj_add_flag(o, LV_OBJ_FLAG_CLICKABLE);
    return o;
}

static lv_obj_t *ring_arc(lv_obj_t *parent, int d, int w) /* the cell rings' counterpart: a faint track, an accent run */
{
    lv_obj_t *a = lv_arc_create(parent);
    lv_obj_set_size(a, d, d);
    lv_obj_align(a, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_remove_flag(a, LV_OBJ_FLAG_CLICKABLE);
    lv_arc_set_bg_angles(a, 0, 360);
    lv_arc_set_rotation(a, 270);
    lv_obj_set_style_bg_opa(a, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_set_style_pad_all(a, 0, LV_PART_KNOB);
    lv_obj_set_style_arc_width(a, w, 0);
    lv_obj_set_style_arc_color(a, lv_color_hex(th->faint), 0);
    lv_obj_set_style_arc_width(a, w, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(a, lv_color_hex(th->accent), LV_PART_INDICATOR);
    return a;
}

static void reopen_app(void) /* the link came or went: build the open phone app again */
{
    if (ph.app) shell_open(ph.app);
}

/* ================= pairing ================= */

static void pair_hide(void)
{
    if (ph.pair_o) lv_obj_delete(ph.pair_o), ph.pair_o = NULL;
}

static void pair_answer(bool ok)
{
    pair_hide();
    send(ok ? PHONE_PAIR_OK : PHONE_PAIR_CANCEL, 0);
    if (ok) pip_phone(PIP_PAIRED); /* a nod */
}

static void pair_ok_cb(lv_event_t *e) { (void)e; pair_answer(true); }

static void pair_show(void)
{
    static const char *const rune[] = {"0001000", "0001100", "1001010", "0101001", "0011010", "0001100",
                                       "0011010", "0101001", "1001010", "0001100", "0001000"};
    static th_cell_t cells[40];
    pair_hide();
    lv_obj_t *o = ph.pair_o = layer();
    th_header(o, "пип-01 // связь", 40);
    if (TH_TERMINAL) th_cells(o, cells, th_pattern(cells, rune, 11, 209, 86, 7, 6, th->text));
    else { /* the Bluetooth rune as one line */
        static const lv_point_precise_t pts[] = {{0, 18}, {34, 48}, {17, 64}, {17, 0}, {34, 16}, {0, 46}};
        lv_obj_t *ln = lv_line_create(o);
        lv_line_set_points(ln, pts, 6);
        lv_obj_set_style_line_width(ln, 4, 0);
        lv_obj_set_style_line_color(ln, lv_color_hex(th->text), 0);
        lv_obj_set_style_line_rounded(ln, true, 0);
        lv_obj_set_pos(ln, SCR / 2 - 17, 92);
    }
    th_text_at(o, th->tiny, th->dim, ph.pair_host ? "сопряжение с компьютером" : "сопряжение с iphone", -1, 177);
    char code[16];
    snprintf(code, sizeof code, "%03u %03u", (unsigned)(ph.code / 1000 % 1000), (unsigned)(ph.code % 1000));
    th_plate(o, 118, 204, 230, 52, th->body, code, false, NULL);
    th_text_at(o, th->tiny, th_mute(), ph.pair_host ? "сверьте код на компьютере" : "сверьте код на телефоне", -1, 273);
    th_button_at(o, 128, 306, 210, hint(TH_UP, "подтвердить"), true, pair_ok_cb);
    footer(o, TH_DOWN " отмена", 371);
}

void phone_link(int state, uint32_t code)
{
    int was = ph.link;
    ph.link = state, ph.code = code;
    if (state == PHONE_PAIRING) ph.pair_host = false, pair_show();
    else if (!ph.pair_host) pair_hide();
    if (state != PHONE_ON) { /* whatever the phone told us is stale now */
        ph.n = 0, ph.tr.has = false;
        if (ph.call.on) phone_call(0, NULL, NULL);
    }
    if (was != state) reopen_app();
}

/* ================= the banner over whatever is on screen ================= */

static void banner_hide(void)
{
    if (ph.banner_t) lv_timer_delete(ph.banner_t), ph.banner_t = NULL;
    if (ph.banner) {
        if (lv_obj_get_user_data(ph.banner)) lv_obj_delete(lv_obj_get_user_data(ph.banner)); /* its hints */
        lv_obj_delete(ph.banner), ph.banner = NULL, pip_glance(0, 0, 0);
    }
}

static void banner_open(void)
{
    ph.open_uid = ph.banner_uid;
    banner_hide();
    shell_open("notify");
}

static void banner_tap(lv_event_t *e) { (void)e; banner_open(); }
static void banner_timeout(lv_timer_t *t) { (void)t; ph.banner_t = NULL; banner_hide(); }
static void height_exec(void *o, int32_t v) { lv_obj_set_height(o, v); }

/* ---------- privacy ---------- */
static int privacy = PHONE_PRIV_HIDE;
static void (*privacy_cb)(int);
static const char *const privacy_word[PHONE_PRIV_COUNT] = {"открыто", "скрыто", "выкл"};
void phone_on_privacy(void (*cb)(int)) { privacy_cb = cb; }
int phone_privacy(void) { return privacy; }

static void lower(char *d, size_t n, const char *s) /* UTF-8 lower case for Latin and Russian, to search in */
{
    size_t k = 0;
    for (const unsigned char *p = (const unsigned char *)s; *p && k + 2 < n;) {
        if (*p >= 'A' && *p <= 'Z') d[k++] = (char)(*p++ + 32);
        else if (p[0] == 0xD0 && p[1] >= 0x90 && p[1] <= 0x9F) d[k++] = (char)0xD0, d[k++] = (char)(p[1] + 0x20), p += 2;
        else if (p[0] == 0xD0 && p[1] >= 0xA0 && p[1] <= 0xAF) d[k++] = (char)0xD1, d[k++] = (char)(p[1] - 0x20), p += 2;
        else if (p[0] == 0xD0 && p[1] == 0x81) d[k++] = (char)0xD1, d[k++] = (char)0x91, p += 2;
        else d[k++] = (char)*p++;
    }
    d[k] = 0;
}

static bool sensitive(const char *app, const char *title, const char *text)
{
    static const char *const words[] = {
        "код", "code", "парол", "password", "passcode", "pin", "otp", "2fa", "одноразов", "подтвержд", "verif",
        "құпиясөз", "банк", "bank", "kaspi", "каспи", "halyk", "халык", "jusan", "freedom", "forte", "форте", "bcc",
        "центркредит", "bereke", "береке", "eurasian", "евразийск", "home credit", "хоум кредит", "egov", "revolut",
        "paypal", "wise", "списан", "зачислен", "перевод", "баланс", "покупка", "оплата", "₸", "тенге", "kzt",
    };
    char all[600], low[600];
    snprintf(all, sizeof all, "%s\n%s\n%s", app, title, text);
    lower(low, sizeof low, all);
    for (size_t i = 0; i < sizeof words / sizeof words[0]; i++)
        if (strstr(low, words[i])) return true;
    return false;
}

/* the sender and the line under it, as a glance may show them */
static const char *glance_title(const note_t *n) { return privacy == PHONE_PRIV_OPEN && !n->secret ? n->title : "новое уведомление"; }
static const char *glance_text(const note_t *n) { return privacy == PHONE_PRIV_OPEN && !n->secret ? n->text : ""; }

static void banner_show(const note_t *n)
{
    banner_hide();
    ph.banner_uid = n->uid;
    lv_obj_t *b = ph.banner = th_box(lv_layer_top(), 93, 52, 280, 100, TH_TERMINAL ? 0 : 16, TH_TERMINAL ? th->bg : th->panel);
    if (TH_TERMINAL) lv_obj_set_style_border_width(b, 2, 0), lv_obj_set_style_border_color(b, lv_color_hex(th->text), 0);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(b, banner_tap, LV_EVENT_CLICKED, NULL);
    th_box(b, 12, 12, 9, 9, TH_TERMINAL ? 0 : LV_RADIUS_CIRCLE, TH_TERMINAL ? th->text : th->accent); /* unread */
    char t[64];
    snprintf(t, sizeof t, "%s // сейчас", n->app);
    lv_obj_t *l = th_text_at(b, th->tiny_bold, th->dim, t, 29, 6);
    lv_obj_set_width(l, 240), lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
    l = th_text_at(b, th->button ? th->button : th->small, th->text, glance_title(n), 12, 30);
    lv_obj_set_width(l, 254), lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
    l = th_text_at(b, th->tiny, th->dim, glance_text(n), 12, 60);
    lv_obj_set_width(l, 254), lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
    lv_obj_set_height(l, lv_font_get_line_height(th->tiny));
    /* drops in like a scan, top down */
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, b);
    lv_anim_set_exec_cb(&a, height_exec);
    lv_anim_set_values(&a, 0, 100);
    lv_anim_set_duration(&a, 180);
    lv_anim_start(&a);
    ph.banner_t = lv_timer_create(banner_timeout, 6000, NULL);
    lv_timer_set_repeat_count(ph.banner_t, 1);
    lv_obj_t *hints = footer(b, TH_UP " открыть   " TH_DOWN " скрыть", 0);
    if (hints) { /* on the top layer, below the banner's own clip */
        lv_obj_set_parent(hints, lv_layer_top());
        lv_obj_align(hints, LV_ALIGN_TOP_MID, 0, 404);
        lv_obj_set_user_data(b, hints);
    }
    pip_glance(SCR / 2, 330, 6); /* Pip lowers his eyes and makes room */
    voice_say(VOICE_HM);
}

/* ================= incoming call ================= */

static void call_hide(void)
{
    if (ph.call_t) lv_timer_delete(ph.call_t), ph.call_t = NULL;
    if (ph.call_o) lv_obj_delete(ph.call_o), ph.call_o = NULL;
}

static void call_answer(bool yes)
{
    ph.call.on = false; /* first: the phone ends the call while we tell it, and that is no missed call */
    send(yes ? PHONE_CALL_ANSWER : PHONE_CALL_DECLINE, ph.call.uid);
    call_hide();
    pip_phone(yes ? PIP_CALL_ANSWERED : PIP_CALL_DECLINED); /* he nods it through, or shakes his head */
}

static void call_yes(lv_event_t *e) { (void)e; call_answer(true); }
static void call_no(lv_event_t *e) { (void)e; call_answer(false); }

static void call_spin(lv_timer_t *t) /* a light runs round the ring while it rings */
{
    (void)t;
    ph.call_phase++;
    if (ph.call_arc) { lv_arc_set_angles(ph.call_arc, ph.call_phase * 6 % 360, ph.call_phase * 6 % 360 + 48); return; }
    for (int i = 0; i < 60; i++) {
        int k = ((i - ph.call_phase) % 4 + 4) % 4;
        th_cell_set(ph.call_grid, &ph.call_cells[i], k == 0 ? th->text : k == 1 ? th->dim : th_off());
    }
}

static void call_show(void)
{
    pip_wake();
    call_hide();
    banner_hide();
    lv_obj_t *o = ph.call_o = layer();
    ph.call_arc = NULL;
    if (TH_TERMINAL) th_ring(ph.call_cells, 60, 210, 8), ph.call_grid = th_cells(o, ph.call_cells, 60);
    else lv_obj_set_y(ph.call_arc = ring_arc(o, 432, 6), 17);
    th_header(o, "входящий вызов", 92);
    th_text_at(o, th->body, th->text, ph.call.name, -1, 160);
    lv_obj_set_style_text_letter_space(th_text_at(o, th->tiny, th->dim, ph.call.kind, -1, 203), 2, 0);
    th_button_at(o, 153, 252, 160, hint(TH_UP, "ответить"), true, call_yes);
    th_button_at(o, 153, 306, 160, hint(TH_DOWN, "отклонить"), false, call_no);
    ph.call_t = lv_timer_create(call_spin, 40, NULL);
}

void phone_call(uint32_t uid, const char *name, const char *kind)
{
    bool rang = ph.call.on;
    ph.call.on = name != NULL;
    if (!name) {
        call_hide();
        if (rang && ph.link == PHONE_ON) pip_phone(PIP_CALL_MISSED); /* it stopped ringing before anyone answered here */
        return;
    }
    ph.call.uid = uid;
    copy(ph.call.name, sizeof ph.call.name, name);
    drawable_only(ph.call.name);
    copy(ph.call.kind, sizeof ph.call.kind, kind ? kind : "");
    call_show();
}

/* ================= the side buttons and where we are ================= */

bool phone_button(int id)
{
    bool top = id == PIP_BTN_PWR;
    if (ph.pair_o) pair_answer(top);
    else if (ph.call_o) call_answer(top);
    else if (ph.banner) top ? banner_open() : banner_hide();
    else return false;
    return true;
}

const char *phone_where(void) { return ph.pair_o ? "pair" : ph.call_o ? "call" : ph.banner ? "banner" : NULL; }

/* ================= no phone yet: what the apps show ================= */

static void offline(lv_obj_t *scr, int icon, const char *name)
{
    if (!TH_TERMINAL) {
        lv_obj_t *img = lv_image_create(scr);
        lv_image_set_src(img, shell_icon(icon));
        lv_obj_align(img, LV_ALIGN_CENTER, 0, -60);
        lv_obj_t *l = th_text(scr, th->small, th->dim, ph.link == PHONE_PAIRING ? "Сверьте код на iPhone" : "Нужен iPhone по Bluetooth.\nПодключите его в настройках.");
        lv_obj_align(l, LV_ALIGN_CENTER, 0, 70);
        return;
    }
    static const char *const pics[3][10] = {
        {"0001000", "0011100", "0111110", "0111110", "0111110", "0111110", "1111111", "1111111", "0000000", "0001000"}, /* bell */
        {"0000111", "0011111", "0111001", "0100001", "0100001", "0100001", "0100111", "1110111", "1110010", "0100000"}, /* note */
        {"0000000", "0011100", "1111111", "1000001", "1011101", "1010101", "1011101", "1000001", "1111111", "0000000"}, /* camera */
    };
    static th_cell_t cells[70];
    char t[48];
    snprintf(t, sizeof t, "пип-01 // %s", name);
    th_header(scr, t, 40);
    int pic = icon == ICON_BELL ? 0 : icon == ICON_MUSIC ? 1 : 2;
    th_cells(scr, cells, th_pattern(cells, pics[pic], 10, 196, 88, 11, 9, th->text));
    th_text_at(scr, th->body, th->text, "нет связи", -1, 214);
    lv_obj_t *l = th_text_at(scr, th->tiny, th->dim, "нужен iphone\nи bluetooth le", -1, 257);
    lv_obj_set_style_text_line_space(l, 4, 0);
    lv_obj_t *row = lv_obj_create(scr);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 4, 0);
    lv_obj_align(row, LV_ALIGN_TOP_MID, 0, 319);
    lv_obj_set_style_text_letter_space(th_text(row, th->tiny, th_mute(), "ожидание пары"), 2, 0);
    th_cursor(row, 9, 16, th_mute());
    footer(scr, TH_DOWN " назад", 393);
}

/* ================= notifications: a wheel of two-line rows, then the whole text ================= */

static struct {
    lv_obj_t *scr, *rows[MAX_NOTES], *grid;
    th_cell_t cells[12];
    float pos, drag_from, vy;
    int focus, tick;
    bool detail;
    uint32_t detail_uid;
    lv_point_t press, last;
    uint32_t press_tick, last_tick;
} nt;

static int note_find(uint32_t uid)
{
    for (int i = 0; i < ph.n; i++)
        if (ph.notes[i].uid == uid) return i;
    return -1;
}

static void nt_layout(void)
{
    int mid = (int)fmaxf(0, fminf(ph.n - 1, roundf(nt.pos)));
    if (mid != nt.tick) { nt.tick = mid; voice_say(VOICE_TICK); }
    nt.focus = mid;
    for (int i = 0; i < ph.n; i++) {
        float d = i - nt.pos;
        lv_obj_set_flag(nt.rows[i], LV_OBJ_FLAG_HIDDEN, fabsf(d) > 1.6f);
        lv_obj_set_y(nt.rows[i], (int32_t)lroundf(SCR / 2 - 32 + d * ROW_H));
        /* the row in the middle wears the plate */
        bool f = i == mid && fabsf(d) < 0.5f;
        lv_obj_t *plate = lv_obj_get_child(nt.rows[i], 0), *top = lv_obj_get_child(nt.rows[i], 2), *bottom = lv_obj_get_child(nt.rows[i], 3);
        lv_obj_set_style_bg_opa(plate, f ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        if (TH_TERMINAL) lv_obj_set_flag(lv_obj_get_child(plate, 0), LV_OBJ_FLAG_HIDDEN, !f); /* the cursor */
        if (TH_TERMINAL) lv_obj_set_style_bg_color(lv_obj_get_child(nt.rows[i], 1), lv_color_hex(f ? th->bg : th->text), 0); /* on the ink plate */
        lv_obj_set_style_text_color(top, lv_color_hex(f ? (TH_TERMINAL ? th->bg : th->dim) : th_mute()), 0);
        lv_obj_set_style_text_color(bottom, lv_color_hex(f ? (TH_TERMINAL ? th->bg : th->text) : th->dim), 0);
        lv_obj_set_style_text_font(top, f ? th->tiny_bold : th->tiny, 0);
        lv_obj_set_style_text_font(bottom, f && th->button ? th->button : th->small, 0);
    }
    for (int i = 0; i < 12 && i < ph.n && nt.grid; i++) th_cell_set(nt.grid, &nt.cells[i], i == mid ? th->text : th_off());
}

static void nt_exec(void *v, int32_t x) { (void)v; nt.pos = x / 1000.0f; nt_layout(); }

static void nt_snap(float target, uint32_t ms)
{
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, &nt);
    lv_anim_set_exec_cb(&a, nt_exec);
    lv_anim_set_values(&a, (int32_t)(nt.pos * 1000), (int32_t)(target * 1000));
    lv_anim_set_duration(&a, ms);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_start(&a);
}

static void nt_build(void);

static void nt_read(lv_event_t *e) /* "прочитано": gone here and on the phone */
{
    (void)e;
    int i = note_find(nt.detail_uid);
    send(PHONE_NOTE_DISMISS, nt.detail_uid);
    if (i >= 0) phone_note_gone(nt.detail_uid); /* rebuilds the list */
    else nt.detail = false, nt_build();
}

static void privacy_tap(lv_event_t *e)
{
    (void)e;
    phone_set_privacy((privacy + 1) % PHONE_PRIV_COUNT);
    if (privacy_cb) privacy_cb(privacy);
}

static void privacy_chip(lv_obj_t *scr) /* under the header: what the screen shows; a tap changes it */
{
    char t[48];
    snprintf(t, sizeof t, "приватность: %s", privacy_word[privacy]);
    lv_obj_t *c = lv_obj_create(scr);
    lv_obj_remove_style_all(c);
    lv_obj_set_size(c, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_hor(c, 12, 0), lv_obj_set_style_pad_ver(c, 4, 0);
    lv_obj_set_style_radius(c, th->radius, 0);
    lv_obj_set_style_border_width(c, 2, 0);
    lv_obj_set_style_border_color(c, lv_color_hex(privacy == PHONE_PRIV_OPEN ? th->accent : th->faint), 0);
    lv_obj_add_flag(c, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(c, privacy_tap, LV_EVENT_CLICKED, NULL);
    th_text(c, th->tiny_bold, privacy == PHONE_PRIV_OPEN ? th->accent : th->dim, t);
    lv_obj_align(c, LV_ALIGN_TOP_MID, 0, 72);
}

static void nt_build(void)
{
    lv_obj_t *scr = nt.scr;
    lv_obj_clean(scr);
    lv_anim_delete(&nt, nt_exec);
    if (ph.link != PHONE_ON) { offline(scr, ICON_BELL, "уведомления"); return; }
    int di = nt.detail ? note_find(nt.detail_uid) : -1;
    if (di >= 0) {
        note_t *n = &ph.notes[di];
        n->unread = false;
        char t[64];
        snprintf(t, sizeof t, "%s // %02d:%02d", n->app, n->hour, n->min);
        th_header(scr, t, 40);
        lv_obj_t *l = th_text_at(scr, th->body, th->text, n->secret ? "скрыто" : n->title, 83, 86); /* one column of 300 px, centred */
        lv_obj_set_width(l, 300), lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
        /* the whole text in a window that scrolls with the finger; a thin bar shows where you are */
        lv_obj_t *win = lv_obj_create(scr);
        lv_obj_remove_style_all(win);
        lv_obj_set_pos(win, 83, 152);
        lv_obj_set_size(win, 310, 4 * (lv_font_get_line_height(th->small) + 6) - 6 + 2); /* four whole lines */
        lv_obj_set_scroll_dir(win, LV_DIR_VER);
        lv_obj_set_scrollbar_mode(win, LV_SCROLLBAR_MODE_ACTIVE);
        lv_obj_set_style_width(win, 3, LV_PART_SCROLLBAR);
        lv_obj_set_style_bg_opa(win, LV_OPA_COVER, LV_PART_SCROLLBAR);
        lv_obj_set_style_bg_color(win, lv_color_hex(th->dim), LV_PART_SCROLLBAR);
        lv_obj_set_style_radius(win, th->radius ? 2 : 0, LV_PART_SCROLLBAR);
        lv_obj_set_style_pad_right(win, 8, 0);
        l = th_text_at(win, th->small, n->secret ? th->dim : th->text, n->secret ? "код, пароль или банк: смотри на телефоне" : n->text, 0, 2);
        lv_obj_set_width(l, 292);
        lv_obj_set_style_text_line_space(l, 6, 0);
        lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
        th_button_at(scr, 128, 300, 210, hint(TH_UP, "прочитано"), true, nt_read);
        footer(scr, TH_DOWN " назад", 365);
        return;
    }
    nt.detail = false;
    th_header(scr, "пип-01 // уведомления", 40);
    privacy_chip(scr);
    if (privacy == PHONE_PRIV_OFF) {
        th_text_at(scr, th->body, th->dim, "выключены", -1, 196);
        lv_obj_set_style_text_letter_space(th_text_at(scr, th->tiny, th_mute(), "с телефона ничего не берём", -1, 240), 2, 0);
        footer(scr, TH_DOWN " назад", 392);
        return;
    }
    if (!ph.n) {
        th_text_at(scr, th->body, th->dim, "пусто", -1, 196);
        lv_obj_set_style_text_letter_space(th_text_at(scr, th->tiny, th_mute(), "новых нет", -1, 240), 2, 0);
        footer(scr, TH_DOWN " назад", 392);
        return;
    }
    for (int i = 0; i < ph.n; i++) {
        note_t *n = &ph.notes[i];
        lv_obj_t *r = nt.rows[i] = lv_obj_create(scr);
        lv_obj_remove_style_all(r);
        lv_obj_set_size(r, 313, 64);
        lv_obj_set_x(r, 70);
        lv_obj_remove_flag(r, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_t *plate = th_box(r, 13, 0, 300, 64, TH_TERMINAL ? 0 : 12, TH_TERMINAL ? th->text : th->panel); /* child 0 */
        if (TH_TERMINAL) lv_obj_set_pos(th_cursor(plate, 12, 18, th->bg), 269, 36);
        th_box(r, 27, 15, 9, 9, TH_TERMINAL ? 0 : LV_RADIUS_CIRCLE, TH_TERMINAL ? th->text : th->accent); /* child 1: unread, inside the plate */
        if (!n->unread) lv_obj_add_flag(lv_obj_get_child(r, 1), LV_OBJ_FLAG_HIDDEN);
        char t[64];
        snprintf(t, sizeof t, "%s // %02d:%02d", n->app, n->hour, n->min);
        lv_obj_t *top = th_text_at(r, th->tiny, th_mute(), t, 45, 6); /* child 2 */
        lv_obj_set_size(top, 222, lv_font_get_line_height(th->tiny_bold)), lv_label_set_long_mode(top, LV_LABEL_LONG_DOT);
        lv_obj_t *bottom = th_text_at(r, th->small, th->dim, glance_title(n), 45, 28); /* child 3 */
        lv_obj_set_size(bottom, 222, lv_font_get_line_height(th->button ? th->button : th->small)), lv_label_set_long_mode(bottom, LV_LABEL_LONG_DOT);
    }
    int count = ph.n < 12 ? ph.n : 12, top = SCR / 2 - (count * 11 - 2) / 2;
    for (int i = 0; i < count; i++) nt.cells[i] = (th_cell_t){405, (int16_t)(top + i * 11), 9, 9, th_off()};
    nt.grid = TH_TERMINAL ? th_cells(scr, nt.cells, count) : NULL; /* the position column is the terminal's */
    footer(scr, TH_UP " открыть   " TH_DOWN " назад", 392);
    nt.pos = (float)(nt.focus < ph.n ? nt.focus : ph.n - 1);
    nt.tick = (int)nt.pos;
    nt_layout();
}

static void nt_open_focused(void)
{
    if (!ph.n) return;
    nt.detail = true;
    nt.detail_uid = ph.notes[nt.focus].uid;
    nt_build();
}

static void nt_input(lv_event_t *e)
{
    if (nt.detail || !ph.n || ph.link != PHONE_ON) return;
    lv_event_code_t code = lv_event_get_code(e);
    lv_point_t p;
    lv_indev_get_point(lv_indev_active(), &p);
    int dx = p.x - nt.press.x, dy = p.y - nt.press.y;
    if (code == LV_EVENT_PRESSED) {
        nt.press = p, nt.press_tick = lv_tick_get(), nt.vy = 0;
        lv_anim_delete(&nt, nt_exec);
        nt.drag_from = nt.pos;
    } else if (code == LV_EVENT_PRESSING) {
        uint32_t el = lv_tick_elaps(nt.last_tick);
        if (el > 0) nt.vy = 0.6f * nt.vy + 0.4f * (p.y - nt.last.y) / el;
        nt.pos = fmaxf(-0.3f, fminf(ph.n - 0.7f, nt.drag_from - dy / (float)ROW_H));
        nt_layout();
    } else if (code == LV_EVENT_RELEASED) {
        if (abs(dx) < 12 && abs(dy) < 12) { /* tap: the plate opens, a neighbour comes to the middle */
            int row = (int)roundf(nt.pos + (p.y - SCR / 2) / (float)ROW_H);
            if (row == nt.focus) nt_open_focused();
            else if (row >= 0 && row < ph.n) nt_snap((float)row, 220);
        } else {
            float target = fmaxf(0, fminf(ph.n - 1, roundf(nt.pos - nt.vy * 330 / ROW_H)));
            nt_snap(target, (uint32_t)fminf(1000, 240 + 110 * fabsf(target - nt.pos)));
        }
    }
    nt.last = p, nt.last_tick = lv_tick_get();
}

void phone_notify_open(lv_obj_t *scr)
{
    ph.app = "notify";
    banner_hide(); /* it is all here now */
    nt.scr = scr;
    nt.detail = false;
    nt.focus = 0;
    int i = ph.open_uid ? note_find(ph.open_uid) : -1; /* came from the banner: straight to that one */
    if (i >= 0) nt.detail = true, nt.detail_uid = ph.open_uid, nt.focus = i;
    ph.open_uid = 0;
    lv_obj_add_flag(scr, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(scr, nt_input, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(scr, nt_input, LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(scr, nt_input, LV_EVENT_RELEASED, NULL);
    nt_build();
}

void phone_notify_close(void)
{
    lv_anim_delete(&nt, nt_exec);
    ph.app = NULL, nt.scr = NULL;
}

bool phone_notify_button(int id)
{
    if (ph.link != PHONE_ON) return false;
    if (nt.detail) {
        if (id == PIP_BTN_PWR) nt_read(NULL);
        else nt.detail = false, nt_build();
        return true;
    }
    if (id != PIP_BTN_PWR) return false;
    nt_open_focused();
    return true;
}

void phone_set_privacy(int mode)
{
    privacy = mode >= 0 && mode < PHONE_PRIV_COUNT ? mode : PHONE_PRIV_HIDE;
    if (privacy == PHONE_PRIV_OFF) ph.n = 0, banner_hide(); /* off: forget what was here too */
    if (nt.scr) nt.detail = false, nt_build();
}

void phone_note(uint32_t uid, const char *app, const char *title, const char *text, int hour, int min, bool quiet)
{
    if (privacy == PHONE_PRIV_OFF) return;
    if (!ph.notes && !(ph.notes = calloc(MAX_NOTES, sizeof *ph.notes))) return;
    int i = note_find(uid);
    if (i < 0) { /* newest first; the oldest falls off the end */
        memmove(&ph.notes[1], &ph.notes[0], sizeof ph.notes[0] * (size_t)(ph.n < MAX_NOTES ? ph.n : MAX_NOTES - 1));
        if (ph.n < MAX_NOTES) ph.n++;
        i = 0;
        if (nt.scr && !nt.detail) nt.focus = nt.focus + 1 < ph.n ? nt.focus + 1 : nt.focus; /* keep the same one in focus */
        if (!quiet) pip_note();
    }
    note_t *n = &ph.notes[i];
    n->uid = uid, n->hour = (uint8_t)hour, n->min = (uint8_t)min, n->unread = true;
    copy(n->app, sizeof n->app, app);
    n->secret = sensitive(app ? app : "", title ? title : "", text ? text : "");
    copy(n->title, sizeof n->title, n->secret ? "" : title); /* a code or a bank is not even kept */
    copy(n->text, sizeof n->text, n->secret ? "" : text);
    drawable_only(n->app), drawable_only(n->title), drawable_only(n->text);
    if (nt.scr) { if (!nt.detail) nt_build(); }
    else if (!quiet && !ph.call_o && !ph.pair_o) banner_show(n);
}

void phone_note_gone(uint32_t uid)
{
    int i = note_find(uid);
    if (i < 0) return;
    memmove(&ph.notes[i], &ph.notes[i + 1], sizeof ph.notes[0] * (size_t)(ph.n - i - 1));
    ph.n--;
    if (ph.banner && ph.banner_uid == uid) banner_hide();
    if (nt.scr) {
        if (nt.detail && nt.detail_uid == uid) nt.detail = false;
        if (nt.focus >= ph.n) nt.focus = ph.n ? ph.n - 1 : 0;
        nt_build();
    }
}

/* ================= music: AMS now-playing, volume by dragging ================= */
/* With a computer connected the same gestures go to it as media keys; it says nothing back, so there is no track. */

static void host_send(int what, int dx, int dy);
static struct {
    lv_obj_t *scr, *head, *title, *artist, *time, *foot, *bar, *vol, *vol_label, *vol_fill;
    lv_timer_t *timer;
    lv_point_t press;
    int drag_y;
    uint32_t vol_until;
    bool pc; /* driving the computer */
    char shown[112];
} mu;

#define BAR_W 262
#define VOL_W 220

static float track_pos(void)
{
    float p = ph.tr.pos + (ph.tr.playing ? lv_tick_elaps(ph.tr.at) / 1000.0f : 0);
    return ph.tr.dur > 0 && p > ph.tr.dur ? (float)ph.tr.dur : p;
}

static void width_exec(void *o, int32_t v) { lv_obj_set_width(o, v); }

static void mu_update(lv_timer_t *t)
{
    (void)t;
    if (!mu.title) return;
    if (mu.pc) { /* only the plate of the last key, for a moment */
        lv_obj_set_flag(mu.vol, LV_OBJ_FLAG_HIDDEN, !(mu.vol_until && (int32_t)(mu.vol_until - lv_tick_get()) > 0));
        return;
    }
    bool has = ph.tr.has, playing = has && ph.tr.playing;
    char s[112];
    snprintf(s, sizeof s, "%d%s%s", playing, has ? ph.tr.title : "", ph.tr.artist);
    if (strcmp(s, mu.shown)) { /* the text only when it changes: a new text restarts the running line */
        strcpy(mu.shown, s);
        lv_label_set_text(mu.head, th_label(!(playing || !has) ? "музыка // пауза" : TH_TERMINAL ? "пип-01 // музыка" : "музыка"));
        lv_label_set_text(mu.title, th_label(has ? ph.tr.title : "ничего не играет"));
        lv_label_set_text(mu.artist, has ? th_label(ph.tr.artist) : "");
        lv_obj_set_style_text_color(mu.title, lv_color_hex(playing || !has ? th->text : th->dim), 0);
        if (mu.foot) lv_label_set_text(mu.foot, th_label(playing ? TH_UP " пауза   " TH_DOWN " назад" : TH_UP " играть   " TH_DOWN " назад"));
    }
    float pos = track_pos();
    lv_obj_set_width(mu.bar, has && ph.tr.dur > 0 ? (int32_t)(pos / ph.tr.dur * BAR_W) : 0);
    lv_obj_set_style_bg_color(mu.bar, lv_color_hex(!playing ? th->dim : TH_TERMINAL ? th->text : th->accent), 0);
    char a[16], b[16];
    if (has) {
        int p = (int)pos, left = ph.tr.dur - p > 0 ? ph.tr.dur - p : 0;
        snprintf(a, sizeof a, "%02d:%02d", p / 60, p % 60);
        snprintf(b, sizeof b, "-%02d:%02d", left / 60, left % 60);
    } else strcpy(a, "--:--"), strcpy(b, "--:--");
    th_leader(mu.time, a, b, BAR_W);
    /* the volume plate while it is being changed; the phone's own word again once the finger is off */
    bool vol = mu.vol_until && (int32_t)(mu.vol_until - lv_tick_get()) > 0;
    if (!vol) ph.vol = ph.vol_phone;
    lv_obj_set_flag(mu.vol, LV_OBJ_FLAG_HIDDEN, !vol);
    if (vol) lv_label_set_text_fmt(mu.vol_label, "%s %d%%", th_label("громкость"), (int)lroundf(ph.vol * 100));
}

static void mu_plate(const char *text) /* the computer's: what the key just did */
{
    lv_label_set_text(mu.vol_label, th_label(text));
    mu.vol_until = lv_tick_get() + 1200;
    mu_update(NULL);
}

static void mu_switch(void *arg) /* iPhone <-> computer: after the event that asked, since it rebuilds the screen */
{
    (void)arg;
    if (ph.app && !strcmp(ph.app, "music")) reopen_app();
}

static void mu_volume_step(bool up)
{
    if (mu.pc) {
        host_send(up ? HOST_VOL_UP : HOST_VOL_DOWN, 0, 0);
        voice_say(VOICE_TICK);
        mu_plate(up ? "громче" : "тише");
        return;
    }
    send(up ? PHONE_VOL_UP : PHONE_VOL_DOWN, 0);
    ph.vol = fmaxf(0, fminf(1, ph.vol + (up ? 1 : -1) / 16.0f)); /* iOS moves in sixteenths */
    mu.vol_until = lv_tick_get() + 1500;
    voice_say(VOICE_TICK);
    lv_anim_t an; /* the fill glides to the new step */
    lv_anim_init(&an);
    lv_anim_set_var(&an, mu.vol_fill);
    lv_anim_set_exec_cb(&an, width_exec);
    lv_anim_set_values(&an, lv_obj_get_width(mu.vol_fill), (int32_t)lroundf(ph.vol * VOL_W));
    lv_anim_set_duration(&an, 120);
    lv_anim_set_path_cb(&an, lv_anim_path_ease_out);
    lv_anim_start(&an);
    mu_update(NULL);
}

static void mu_input(lv_event_t *e)
{
    if (!mu.pc && ph.link != PHONE_ON) return;
    lv_event_code_t code = lv_event_get_code(e);
    lv_point_t p;
    lv_indev_get_point(lv_indev_active(), &p);
    if (code == LV_EVENT_PRESSED) {
        mu.press = p, mu.drag_y = p.y;
    } else if (code == LV_EVENT_PRESSING) {
        /* up or down: a volume step every 18 px, like turning a knob */
        if (abs(p.y - mu.press.y) > abs(p.x - mu.press.x))
            while (abs(p.y - mu.drag_y) >= 18) {
                bool up = p.y < mu.drag_y;
                mu.drag_y += up ? -18 : 18;
                if (!mu.pc && !(mu.vol_until && (int32_t)(mu.vol_until - lv_tick_get()) > 0)) lv_obj_set_width(mu.vol_fill, (int32_t)lroundf(ph.vol * VOL_W)); /* the plate was down */
                mu_volume_step(up);
            }
    } else if (code == LV_EVENT_RELEASED) {
        int dx = p.x - mu.press.x, dy = p.y - mu.press.y;
        if (abs(dx) > 90 && abs(dy) < abs(dx) / 2) { /* swipe: the next or last track */
            if (!mu.pc) send(dx < 0 ? PHONE_NEXT : PHONE_PREV, 0);
            else host_send(dx < 0 ? HOST_TRACK_NEXT : HOST_TRACK_PREV, 0, 0), mu_plate(dx < 0 ? "следующий трек" : "предыдущий трек");
        } else if (abs(dx) < 12 && abs(dy) < 12 && ph.link == PHONE_ON && ph.host == HOST_ON) { /* a tap: the other one */
            ph.mu_phone = mu.pc;
            lv_async_call(mu_switch, NULL);
        }
    }
}

void phone_music_open(lv_obj_t *scr)
{
    ph.app = "music";
    mu.scr = scr, mu.title = NULL, mu.vol_fill = NULL, mu.vol_until = 0, mu.shown[0] = 1;
    mu.pc = ph.host == HOST_ON && (ph.link != PHONE_ON || !ph.mu_phone);
    if (!mu.pc && ph.link != PHONE_ON) { offline(scr, ICON_MUSIC, "музыка"); return; }
    mu.head = th_header(scr, "", 40);
    /* the title big; a long one runs */
    const lv_font_t *big = th->title ? th->title : th->body;
    mu.title = th_text(scr, big, th->text, "");
    lv_obj_set_width(mu.title, 420), lv_label_set_long_mode(mu.title, LV_LABEL_LONG_SCROLL_CIRCULAR); /* clipped near the rim */
    lv_obj_align(mu.title, LV_ALIGN_TOP_MID, 0, 128);
    mu.artist = th_text_at(scr, th->small, th->dim, "", -1, 0);
    lv_obj_set_style_text_letter_space(mu.artist, 2, 0);
    lv_obj_set_width(mu.artist, 300), lv_label_set_long_mode(mu.artist, LV_LABEL_LONG_DOT);
    lv_obj_align(mu.artist, LV_ALIGN_TOP_MID, 0, 190);
    if (mu.pc) { /* the computer: the keys and the gestures, no track */
        lv_label_set_text(mu.head, th_label(TH_TERMINAL ? "пип-01 // компьютер" : "музыка // компьютер"));
        lv_label_set_text(mu.title, th_label("компьютер"));
        lv_obj_set_style_text_line_space(th_text_at(scr, th->tiny, th->dim, "свайп: трек\nвверх / вниз: громкость", -1, 236), 4, 0);
        footer(scr, TH_UP " играть / пауза   " TH_DOWN " назад", 375);
    } else {
        int bh = TH_TERMINAL ? 9 : 4;
        th_box(scr, SCR / 2 - BAR_W / 2, 250, BAR_W, bh, th->radius ? bh / 2 : 0, th_off()); /* the track's run */
        mu.bar = th_box(scr, SCR / 2 - BAR_W / 2, 250, 0, bh, th->radius ? bh / 2 : 0, th->accent);
        mu.time = th_text_at(scr, th->tiny, th_mute(), "", SCR / 2 - BAR_W / 2, 266);
        mu.foot = footer(scr, "", 375);
    }
    if (ph.link == PHONE_ON && ph.host == HOST_ON) /* both here: a tap sends the music app to the other one */
        th_text_at(scr, th->tiny, th_mute(), mu.pc ? "тап: iphone" : "тап: компьютер", -1, TH_TERMINAL ? 330 : 318);
    /* the volume plate over the title while it changes (the computer's: what the last key did) */
    uint32_t plate = TH_TERMINAL ? th->text : th->panel, ink = TH_TERMINAL ? th->bg : th->text;
    mu.vol = th_box(scr, SCR / 2 - 130, 108, 260, 92, th->radius ? 18 : 0, plate);
    mu.vol_label = th_text_at(mu.vol, th->button ? th->button : th->small, ink, "", -1, mu.pc ? 32 : 14);
    if (!mu.pc) {
        th_box(mu.vol, 20, 58, VOL_W, 10, th->radius ? 5 : 0, TH_TERMINAL ? th->dim : th->faint);
        mu.vol_fill = th_box(mu.vol, 20, 58, 0, 10, th->radius ? 5 : 0, TH_TERMINAL ? th->bg : th->accent);
    }
    lv_obj_add_flag(mu.vol, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(scr, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(scr, mu_input, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(scr, mu_input, LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(scr, mu_input, LV_EVENT_RELEASED, NULL);
    mu_update(NULL);
    mu.timer = lv_timer_create(mu_update, 250, NULL);
}

void phone_music_close(void)
{
    if (mu.timer) lv_timer_delete(mu.timer), mu.timer = NULL;
    if (mu.vol_fill) lv_anim_delete(mu.vol_fill, width_exec);
    mu.title = NULL, mu.vol_fill = NULL, ph.app = NULL;
}

bool phone_music_button(int id)
{
    if (mu.title && mu.pc) {
        if (id != PIP_BTN_PWR) return false;
        host_send(HOST_PLAY_PAUSE, 0, 0);
        mu_plate("играть / пауза");
        return true;
    }
    if (ph.link != PHONE_ON || id != PIP_BTN_PWR) return false;
    if (ph.tr.has) { /* show it at once; the phone confirms (or corrects) right after */
        ph.tr.pos = track_pos(), ph.tr.at = lv_tick_get();
        ph.tr.playing = !ph.tr.playing;
    }
    send(PHONE_PLAY_PAUSE, 0);
    mu_update(NULL);
    return true;
}

void phone_track(const char *title, const char *artist, int dur_s, float pos_s, bool playing)
{
    ph.tr.has = title != NULL;
    copy(ph.tr.title, sizeof ph.tr.title, title);
    copy(ph.tr.artist, sizeof ph.tr.artist, artist);
    drawable_only(ph.tr.title), drawable_only(ph.tr.artist);
    ph.tr.dur = dur_s, ph.tr.pos = pos_s, ph.tr.playing = playing, ph.tr.at = lv_tick_get();
    mu_update(NULL);
}

void phone_volume(float v) /* while the finger is on it ours wins: the phone's echoes of the last steps come late */
{
    ph.vol_phone = fmaxf(0, fminf(1, v));
    if (!(mu.vol_until && (int32_t)(mu.vol_until - lv_tick_get()) > 0)) ph.vol = ph.vol_phone;
    mu_update(NULL);
}

/* ================= camera: the phone's shutter, now or after a countdown ================= */

enum { CAM_RING = 0, CAM_DISC = 28, CAM_CORNERS = 28 + 97, CAM_N = 28 + 97 + 20 };
enum { CD_ONE = 0, CD_TWO = 35, CD_RING = 105, CD_N = 165 };

static struct {
    lv_obj_t *scr, *ready, *count, *chips[3], *grid, *cgrid, *num, *arc;
    th_cell_t cells[CAM_N], cd[CD_N];
    lv_timer_t *timer;
    int delay, left_ms, total_ms;
    uint32_t start;
} cam = {.delay = 1};

static const int cam_delays[3] = {0, 3, 10};

static void cam_chips(void)
{
    for (int i = 0; i < 3; i++) {
        bool on = i == cam.delay;
        lv_obj_set_style_bg_opa(cam.chips[i], on ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        lv_obj_set_style_bg_color(cam.chips[i], lv_color_hex(TH_TERMINAL ? th->text : th->accent), 0);
        lv_obj_set_style_border_width(cam.chips[i], on ? 0 : 2, 0);
        lv_obj_set_style_text_color(lv_obj_get_child(cam.chips[i], 0), lv_color_hex(on ? th->bg : th->dim), 0);
    }
}

static void cam_chip(lv_event_t *e)
{
    cam.delay = (int)(intptr_t)lv_event_get_user_data(e);
    cam_chips();
}

static void cam_shoot(void)
{
    send(PHONE_SHUTTER, 0);
    /* one frame of ink over everything: the "flash" */
    lv_obj_t *f = th_box(cam.scr, 0, 0, SCR, SCR, 0, th->text);
    lv_obj_delete_delayed(f, 80);
    voice_say(VOICE_TICK);
}

static void cam_show_count(bool on)
{
    lv_obj_set_flag(cam.ready, LV_OBJ_FLAG_HIDDEN, on);
    lv_obj_set_flag(cam.count, LV_OBJ_FLAG_HIDDEN, !on);
}

static void cam_tick(lv_timer_t *t)
{
    (void)t;
    cam.left_ms = cam.total_ms - (int)lv_tick_elaps(cam.start); /* by the clock: timer ticks can come late */
    if (cam.left_ms <= 0) {
        lv_timer_delete(cam.timer), cam.timer = NULL;
        cam_show_count(false);
        cam_shoot();
        return;
    }
    int sec = (cam.left_ms + 999) / 1000;
    char d[12];
    snprintf(d, sizeof d, "%d", sec);
    if (!TH_TERMINAL) {
        lv_label_set_text(cam.num, d);
        lv_arc_set_angles(cam.arc, 0, (lv_value_precise_t)(360 * cam.left_ms / cam.total_ms));
        return;
    }
    if (sec >= 10) th_blocks_hide(cam.cgrid, cam.cd + CD_ONE, 35), th_blocks_show(cam.cgrid, cam.cd + CD_TWO, d, th->text);
    else th_blocks_hide(cam.cgrid, cam.cd + CD_TWO, 70), th_blocks_show(cam.cgrid, cam.cd + CD_ONE, d, th->text);
    int lit = cam.left_ms * 60 / cam.total_ms;
    for (int i = 0; i < 60; i++) th_cell_set(cam.cgrid, &cam.cd[CD_RING + i], i < lit ? th->text : th_off());
}

static void cam_go(void)
{
    if (cam.timer) return;
    if (!cam_delays[cam.delay]) { cam_shoot(); return; }
    cam.total_ms = cam.left_ms = cam_delays[cam.delay] * 1000;
    cam.start = lv_tick_get();
    cam_show_count(true);
    cam.timer = lv_timer_create(cam_tick, 50, NULL);
    cam_tick(NULL);
}

static void cam_tap(lv_event_t *e)
{
    (void)e;
    lv_point_t p;
    lv_indev_get_point(lv_indev_active(), &p);
    if (hypotf(p.x - SCR / 2, p.y - 176) < 92) cam_go(); /* the shutter itself */
}

void phone_camera_open(lv_obj_t *scr)
{
    ph.app = "camera";
    cam.scr = scr, cam.timer = NULL;
    if (ph.link != PHONE_ON) { offline(scr, ICON_CAMERA, "камера"); return; }
    /* ready: a shutter of cells in a viewfinder, the timer chips */
    lv_obj_t *r = cam.ready = th_box(scr, 0, 0, SCR, SCR, 0, 0);
    lv_obj_set_style_bg_opa(r, LV_OPA_TRANSP, 0);
    th_header(r, "пип-01 // камера", 40);
    int n = 0;
    if (!TH_TERMINAL) { /* a shutter button: an ink ring round an accent disc */
        lv_obj_t *ring = th_box(r, SCR / 2 - 80, 96, 160, 160, LV_RADIUS_CIRCLE, th->bg);
        lv_obj_set_style_border_width(ring, 6, 0);
        lv_obj_set_style_border_color(ring, lv_color_hex(th->text), 0);
        th_box(r, SCR / 2 - 62, 114, 124, 124, LV_RADIUS_CIRCLE, th->accent);
    } else {
    for (int i = 0; i < 28; i++) {
        float a = i / 28.0f * 6.2831853f;
        cam.cells[n++] = (th_cell_t){(int16_t)lroundf(SCR / 2 + cosf(a) * 80 - 4), (int16_t)lroundf(176 + sinf(a) * 80 - 4), 8, 8, th->text};
    }
    for (int y = -5; y <= 5; y++)
        for (int x = -5; x <= 5; x++)
            if (hypotf((float)x, (float)y) <= 5.2f) cam.cells[n++] = (th_cell_t){(int16_t)(229 + x * 11), (int16_t)(172 + y * 11), 9, 9, th->dim};
    static const int corners[4][4] = {{118, 92, 1, 1}, {339, 92, -1, 1}, {118, 252, 1, -1}, {339, 252, -1, -1}};
    for (int c = 0; c < 4; c++)
        for (int k = 0; k < 3; k++) {
            cam.cells[n++] = (th_cell_t){(int16_t)(corners[c][0] + corners[c][2] * k * 11), (int16_t)corners[c][1], 9, 9, th_mute()};
            if (k) cam.cells[n++] = (th_cell_t){(int16_t)corners[c][0], (int16_t)(corners[c][1] + corners[c][3] * k * 11), 9, 9, th_mute()};
        }
    cam.grid = th_cells(r, cam.cells, n);
    }
    lv_obj_set_style_text_letter_space(th_text_at(r, th->tiny, th_mute(), "таймер", -1, 273), 2, 0);
    lv_obj_t *row = lv_obj_create(r);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(row, 10, 0);
    lv_obj_align(row, LV_ALIGN_TOP_MID, 0, 300);
    static const char *const names[3] = {"выкл", "3 с", "10 с"};
    for (int i = 0; i < 3; i++) {
        lv_obj_t *c = cam.chips[i] = lv_obj_create(row);
        lv_obj_remove_style_all(c);
        lv_obj_set_size(c, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_set_style_pad_hor(c, 10, 0);
        lv_obj_set_style_pad_ver(c, 4, 0);
        lv_obj_set_style_radius(c, th->radius, 0);
        lv_obj_set_style_border_color(c, lv_color_hex(th->faint), 0);
        lv_obj_add_flag(c, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(c, cam_chip, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        th_text(c, th->tiny_bold, th->dim, names[i]);
    }
    cam_chips();
    footer(r, TH_UP " снимок   " TH_DOWN " назад", 375);
    /* the countdown: one big digit (or two), the ring running out */
    lv_obj_t *k = cam.count = th_box(scr, 0, 0, SCR, SCR, 0, 0);
    lv_obj_set_style_bg_opa(k, LV_OPA_TRANSP, 0);
    th_header(k, "камера // таймер", 92);
    if (TH_TERMINAL) {
        th_blocks(cam.cd + CD_ONE, 189, 136, "0", 18, 16);
        th_blocks(cam.cd + CD_TWO, 135, 136, "00", 18, 16);
        th_ring(cam.cd + CD_RING, 60, 196, 9);
        cam.cgrid = th_cells(k, cam.cd, CD_N);
    } else {
        lv_obj_set_y(cam.arc = ring_arc(k, 404, 8), 31);
        cam.num = th_text(k, th->digits, th->text, "");
        lv_obj_align(cam.num, LV_ALIGN_TOP_MID, 0, 150);
    }
    lv_obj_set_style_text_letter_space(th_text_at(k, th->tiny, th->dim, "сек до снимка", -1, 283), 2, 0);
    footer(k, TH_DOWN " отмена", 347);
    cam_show_count(false);
    lv_obj_add_flag(scr, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(scr, cam_tap, LV_EVENT_CLICKED, NULL);
}

void phone_camera_close(void)
{
    if (cam.timer) lv_timer_delete(cam.timer), cam.timer = NULL;
    ph.app = NULL;
}

bool phone_camera_button(int id)
{
    if (ph.link != PHONE_ON) return false;
    if (id == PIP_BTN_PWR) { cam_go(); return true; }
    if (!cam.timer) return false;
    lv_timer_delete(cam.timer), cam.timer = NULL; /* bottom during the countdown: cancel */
    cam_show_count(false);
    return true;
}

/* ================= the remote: slides, a pointer and scrolling for a computer ================= */
/* The side buttons turn the slides (page keys); holding the bottom one scrolls: raise Pip and the page goes down, lower
   it and it comes back, as a touchpad does. A finger held on the screen lets the pointer follow the hand (the
   gyroscope), a tap clicks. Swipe down to leave, as from any app. */

#define RM_GAIN 16.0f   /* pointer px per degree the hand turns */
#define RM_SCROLL 0.6f  /* wheel notches per degree the hand tips while the bottom key is held */
#define RM_DEAD 1.5f    /* deg/s: slower is the hand's tremor and the gyroscope's drift */
#define RM_TAP_MS 250   /* a touch shorter than this is a click */
#define RM_DOUBLE_MS 300 /* a second touch this soon after a tap: a double click, or held, a drag */
#define RM_HOLD_MS 300  /* the bottom key held longer scrolls instead of turning back */
enum { RM_IDLE, RM_POINTER, RM_SCROLLING };
static struct {
    lv_obj_t *scr, *pad, *pad_text, *flash, *grid;
    th_cell_t ring[40];
    lv_timer_t *flash_t, *click_t; /* click_t: a tap's click, held back in case a second touch makes it a drag */
    float bias[3], up[3], fx, fy, sy; /* the gyroscope's drift, where up is (device axes), fractions of a px and a notch */
    bool has_up, down, key, scrolling; /* a finger on the glass, the bottom key down, and held long enough to scroll */
    bool second, dragging;             /* this touch came right after a tap; the button is held for it */
    uint32_t down_at, key_at;
} rm;

void phone_on_host(void (*cb)(int, int, int)) { ph.host_cb = cb; }
int phone_host_state(void) { return ph.host; }

void phone_host(int state, uint32_t code)
{
    int was = ph.host;
    if (state == HOST_PAIRING) ph.code = code, ph.pair_host = true, pair_show();
    else if (ph.pair_host) ph.pair_host = false, pair_hide();
    ph.host = state;
    if (was != state && ph.app && (!strcmp(ph.app, "remote") || !strcmp(ph.app, "music"))) reopen_app();
}

static void host_send(int what, int dx, int dy)
{
    if (ph.host_cb && ph.host == HOST_ON) ph.host_cb(what, dx, dy);
}

static void rm_flash_off(lv_timer_t *t)
{
    (void)t;
    rm.flash_t = NULL;
    lv_label_set_text(rm.flash, "");
}

static void rm_flash(const char *text) /* what the last press did, for a moment */
{
    lv_label_set_text(rm.flash, th_label(text));
    if (rm.flash_t) lv_timer_reset(rm.flash_t);
    else rm.flash_t = lv_timer_create(rm_flash_off, 700, NULL), lv_timer_set_repeat_count(rm.flash_t, 1);
}

static void rm_pad(int mode) /* RM_*: the pad lights while the hand leads the pointer or the page */
{
    bool on = mode != RM_IDLE;
    if (TH_TERMINAL) {
        for (int i = 0; i < 40; i++) th_cell_set(rm.grid, &rm.ring[i], on ? th->text : th_mute());
    } else {
        lv_obj_set_style_border_color(rm.pad, lv_color_hex(on ? th->accent : th->faint), 0);
    }
    lv_label_set_text(rm.pad_text, th_label(mode == RM_POINTER ? "указка" : mode == RM_SCROLLING ? "скролл" : "держи палец:\nуказка"));
}

static void rm_click_now(lv_timer_t *t) /* no second touch came: it was a plain click */
{
    (void)t;
    rm.click_t = NULL;
    host_send(HOST_CLICK, 0, 0), rm_flash("клик");
}

static void rm_drag(bool on)
{
    if (rm.dragging == on) return;
    rm.dragging = on;
    host_send(on ? HOST_DOWN : HOST_UP, 0, 0);
    lv_label_set_text(rm.pad_text, th_label(on ? "тяну" : rm.down ? "указка" : "держи палец:\nуказка"));
}

/* tap: a click (a moment late: a second touch may follow); tap twice: a double click; tap, then touch and hold:
   the button stays down while the hand moves the pointer, and lets go with the finger - drag a window, files */
static void rm_touch(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_PRESSED) {
        rm.down = true, rm.down_at = lv_tick_get(), rm.fx = rm.fy = 0;
        rm.second = rm.click_t != NULL;
        if (rm.click_t) lv_timer_delete(rm.click_t), rm.click_t = NULL;
        if (!rm.scrolling) rm_pad(RM_POINTER);
    } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        bool tap = code == LV_EVENT_RELEASED && lv_tick_elaps(rm.down_at) < RM_TAP_MS;
        rm.down = false;
        if (!rm.scrolling) rm_pad(RM_IDLE);
        if (rm.dragging) rm_drag(false), rm_flash("отпустил");
        else if (rm.second && tap) host_send(HOST_CLICK, 0, 0), host_send(HOST_CLICK, 0, 0), rm_flash("двойной клик");
        else if (tap) rm.click_t = lv_timer_create(rm_click_now, RM_DOUBLE_MS, NULL), lv_timer_set_repeat_count(rm.click_t, 1);
        rm.second = false;
    }
}

void phone_remote_imu(float ax, float ay, float az, float dt) /* gravity, low-passed: which way the hand holds it */
{
    float k = rm.has_up ? fminf(1, dt * 4) : 1;
    rm.up[0] += (ax - rm.up[0]) * k, rm.up[1] += (ay - rm.up[1]) * k, rm.up[2] += (az - rm.up[2]) * k;
    rm.has_up = true;
}

void phone_remote_gyro(float gx, float gy, float gz, float dt)
{
    float w[3] = {gx - rm.bias[0], gy - rm.bias[1], gz - rm.bias[2]};
    if (rm.key && !rm.scrolling && lv_tick_elaps(rm.key_at) >= RM_HOLD_MS) rm.scrolling = true, rm.sy = 0, rm_pad(RM_SCROLLING);
    if (!rm.down && !rm.scrolling) { /* nothing leads: learn the drift, from the quiet moments only */
        if (fabsf(w[0]) < 8 && fabsf(w[1]) < 8 && fabsf(w[2]) < 8) {
            float k = 1 - expf(-dt / 2.0f);
            for (int i = 0; i < 3; i++) rm.bias[i] += w[i] * k;
        }
        return;
    }
    /* turning about the vertical moves the pointer sideways, tipping about the horizontal "right" moves it up and down,
       however the hand holds it: upright facing you, or flat in the palm */
    float u[3] = {0, 1, 0}, n = sqrtf(rm.up[0] * rm.up[0] + rm.up[1] * rm.up[1] + rm.up[2] * rm.up[2]);
    if (n > 0.3f) u[0] = rm.up[0] / n, u[1] = rm.up[1] / n, u[2] = rm.up[2] / n;
    float r[3] = {1 - u[0] * u[0], -u[0] * u[1], -u[0] * u[2]}; /* the device's x, levelled */
    if (fabsf(u[0]) > 0.9f) r[0] = 0, r[1] = -u[0], r[2] = 0;   /* on its side: x points up, take the screen's y */
    float m = sqrtf(r[0] * r[0] + r[1] * r[1] + r[2] * r[2]);
    float yaw = w[0] * u[0] + w[1] * u[1] + w[2] * u[2], pitch = (w[0] * r[0] + w[1] * r[1] + w[2] * r[2]) / m;
    if (fabsf(yaw) < RM_DEAD) yaw = 0;
    if (fabsf(pitch) < RM_DEAD) pitch = 0;
    if (rm.scrolling) { /* raised (tipped back), the page goes down: the content follows the hand */
        rm.sy += pitch * RM_SCROLL * dt;
        int n = (int)rm.sy;
        if (n) host_send(HOST_SCROLL, 0, n), rm.sy -= n;
        return;
    }
    if (rm.down && rm.second && !rm.dragging && lv_tick_elaps(rm.down_at) >= RM_TAP_MS) rm_drag(true); /* tapped, then held */
    rm.fx += -yaw * RM_GAIN * dt, rm.fy += -pitch * RM_GAIN * dt; /* right-handed: turning right is negative about up */
    int dx = (int)rm.fx, dy = (int)rm.fy;
    if (dx || dy) host_send(HOST_MOVE, dx, dy), rm.fx -= dx, rm.fy -= dy;
}

void phone_remote_open(lv_obj_t *scr)
{
    ph.app = "remote";
    float bias[3] = {rm.bias[0], rm.bias[1], rm.bias[2]}; /* the drift outlives the screen */
    memset(&rm, 0, sizeof rm);
    memcpy(rm.bias, bias, sizeof bias);
    rm.scr = scr;
    th_header(scr, "пип-01 // пульт", 40);
    if (ph.host != HOST_ON) { /* how to get a computer here */
        if (!TH_TERMINAL) {
            lv_obj_t *img = lv_image_create(scr);
            lv_image_set_src(img, shell_icon(ICON_REMOTE));
            lv_obj_align(img, LV_ALIGN_CENTER, 0, -60);
        } else {
            th_text_at(scr, th->body, th->text, "нет связи", -1, 150);
        }
        lv_obj_t *l = th_text_at(scr, th->tiny, th->dim,
                                 ph.host == HOST_PAIRING ? "сверьте код на компьютере"
                                                         : "на компьютере: bluetooth,\nустройство pip.\nпульт листает слайды\nи водит указку",
                                 -1, TH_TERMINAL ? 200 : 260);
        lv_obj_set_style_text_line_space(l, 4, 0);
        footer(scr, TH_DOWN " назад", 404);
        return;
    }
    if (TH_TERMINAL) { /* the pad: a ring of cells */
        th_ring(rm.ring, 40, 104, 9);
        rm.grid = th_cells(scr, rm.ring, 40);
    } else {
        rm.pad = th_box(scr, SCR / 2 - 104, SCR / 2 - 104, 208, 208, LV_RADIUS_CIRCLE, th->bg);
        lv_obj_set_style_bg_opa(rm.pad, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(rm.pad, 6, 0);
        lv_obj_remove_flag(rm.pad, LV_OBJ_FLAG_CLICKABLE);
    }
    rm.pad_text = th_text_at(scr, th->small, th->dim, "", -1, SCR / 2 - 30);
    lv_obj_set_style_text_align(rm.pad_text, LV_TEXT_ALIGN_CENTER, 0);
    rm.flash = th_text_at(scr, th->body, th->text, "", -1, SCR / 2 + 30);
    rm_pad(RM_IDLE);
    if (TH_TERMINAL) footer(scr, TH_UP " дальше   " TH_DOWN " назад", 404), th_text_at(scr, th->tiny, th->dim, "держи нижнюю: скролл", -1, 372);
    else lv_obj_set_style_text_line_space(th_text_at(scr, th->tiny, th->dim, "кнопки: дальше / назад\nдержи нижнюю: скролл", -1, 366), 4, 0);
    lv_obj_add_flag(scr, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(scr, rm_touch, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(scr, rm_touch, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(scr, rm_touch, LV_EVENT_PRESS_LOST, NULL);
}

void phone_remote_close(void)
{
    if (rm.flash_t) lv_timer_delete(rm.flash_t), rm.flash_t = NULL;
    if (rm.click_t) lv_timer_delete(rm.click_t), rm.click_t = NULL;
    if (rm.dragging) rm_drag(false); /* never leave the computer's button held */
    rm.scr = NULL, rm.down = rm.key = rm.scrolling = false;
    ph.app = NULL;
}

bool phone_remote_button(int id) /* top: the next slide; bottom: wait to see if it is held (without a computer: leave) */
{
    if (ph.host != HOST_ON) return false;
    if (id == PIP_BTN_PWR) host_send(HOST_NEXT, 0, 0), rm_flash("дальше");
    else rm.key = true, rm.key_at = lv_tick_get(), rm.scrolling = false;
    return true;
}

void phone_remote_button_up(int id) /* the bottom key let go: quick, the slide before; held, the scrolling ends */
{
    if (id != PIP_BTN_BOOT || !rm.key || !rm.scr) return;
    bool tap = !rm.scrolling && lv_tick_elaps(rm.key_at) < RM_HOLD_MS;
    rm.key = rm.scrolling = false;
    rm_pad(rm.down ? RM_POINTER : RM_IDLE);
    if (tap) host_send(HOST_PREV, 0, 0), rm_flash("назад");
}

/* ================= Bluetooth: who is connected and how, a reconnect, forgetting everyone ================= */

static struct {
    void (*query)(phone_bt_t *);
    void (*act)(int);
    lv_obj_t *scr, *rows[3][3], *info, *forget; /* per row: the name, a line, the phone's second line */
    lv_timer_t *timer;
    uint32_t forget_at; /* the first tap on "забыть всё": a second within 3 s does it */
} bt;

void phone_on_bt(void (*query)(phone_bt_t *), void (*act)(int)) { bt.query = query, bt.act = act; }

static void bt_update(lv_timer_t *t)
{
    (void)t;
    phone_bt_t s = {0};
    if (bt.query) bt.query(&s);
    static const char *const who[3] = {"iphone", "компьютер", "отладка"};
    for (int r = 0; r < 3; r++) {
        const phone_peer_t *p = NULL;
        for (int i = 0; i < s.n; i++)
            if (s.peer[i].role == r) p = &s.peer[i];
        char a[64], d[128], d2[96] = "";
        bool good = p && p->encrypted && (r != PEER_PHONE || p->ready);
        snprintf(a, sizeof a, "%s%s", who[r], good ? "" : p ? " · подключается" : ""); /* the colour says it too */
        if (!p) snprintf(d, sizeof d, "%s", r == PEER_DEBUG ? "не подключена" : "не подключён");
        else {
            int k = snprintf(d, sizeof d, "%s", p->encrypted ? "шифр" : "без шифра");
            if (p->rssi) k += snprintf(d + k, sizeof d - k, " · %d дБм", p->rssi);
            snprintf(d + k, sizeof d - k, " · %d мин", p->secs / 60);
            if (r == PEER_PHONE && !p->ready) snprintf(d2, sizeof d2, "ищу службы...");
            else if (r == PEER_PHONE) /* what works, one word each */
                snprintf(d2, sizeof d2, "%s%s%s%s%s", p->ancs ? "уведомления" : "", p->ancs && (p->ams || p->cts) ? ", " : "",
                         p->ams ? "музыка" : "", p->ams && p->cts ? ", " : "", p->cts ? "время" : ""),
                    d2[0] || snprintf(d2, sizeof d2, "службы не найдены");
        }
        lv_label_set_text(bt.rows[r][0], th_label(a));
        lv_obj_set_style_text_color(bt.rows[r][0], lv_color_hex(good ? th->text : p ? th->accent : th->dim), 0);
        lv_label_set_text(bt.rows[r][1], th_label(d));
        if (bt.rows[r][2]) lv_label_set_text(bt.rows[r][2], th_label(d2));
    }
    int unknown = 0;
    for (int i = 0; i < s.n; i++) unknown += s.peer[i].role == PEER_UNKNOWN;
    char i[96];
    snprintf(i, sizeof i, "виден новым: %s · сопряжено: %d%s", s.advertising ? "да" : "нет", s.bonds, unknown ? " · кто-то опознаётся" : "");
    lv_label_set_text(bt.info, th_label(i));
    if (bt.forget_at && lv_tick_elaps(bt.forget_at) > 3000)
        bt.forget_at = 0, lv_label_set_text(lv_obj_get_child(bt.forget, 0), th_label("забыть всё"));
}

static void bt_reconnect(lv_event_t *e) { (void)e; if (bt.act) bt.act(BT_RECONNECT_PHONE); }

static void bt_forget(lv_event_t *e)
{
    (void)e;
    if (!bt.forget_at) { bt.forget_at = lv_tick_get() | 1; lv_label_set_text(lv_obj_get_child(bt.forget, 0), th_label("точно?")); return; }
    bt.forget_at = 0;
    lv_label_set_text(lv_obj_get_child(bt.forget, 0), th_label("забыть всё"));
    if (bt.act) bt.act(BT_FORGET_ALL);
}

void phone_bt_open(lv_obj_t *scr)
{
    bt.scr = scr, bt.forget_at = 0;
    th_header(scr, "пип-01 // bluetooth", 40);
    static const int top[3] = {78, 160, 218}; /* the phone's row has a second line */
    for (int r = 0; r < 3; r++) {
        bt.rows[r][0] = th_text_at(scr, th->small, th->text, "", 66, top[r]);
        for (int k = 1; k < (r ? 2 : 3); k++) {
            lv_obj_t *l = bt.rows[r][k] = th_text_at(scr, th->tiny, th->dim, "", 66, top[r] + 2 + k * 24);
            lv_obj_set_size(l, 340, lv_font_get_line_height(th->tiny)), lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
        }
        if (r) bt.rows[r][2] = NULL;
    }
    bt.info = th_text_at(scr, th->tiny, th_mute(), "", -1, 278);
    th_button_at(scr, 40, 312, 212, "переподключить", true, bt_reconnect);
    bt.forget = th_button_at(scr, 262, 312, 160, "забыть всё", false, bt_forget);
    bt_update(NULL);
    bt.timer = lv_timer_create(bt_update, 1000, NULL);
}

void phone_bt_close(void)
{
    if (bt.timer) lv_timer_delete(bt.timer), bt.timer = NULL;
}
