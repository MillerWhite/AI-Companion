#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_heap_caps.h"
#include "esp_netif.h"
#include "esp_netif_ip_addr.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "bsp/esp-bsp.h"
#include "bsp/display.h"
#include "lvgl.h"
#include "cJSON.h"

#include "cloud_client.h"
#include "companion_audio.h"
#include "companion_emotion.h"
#include "companion_imu.h"
#include "companion_motion.h"
#include "companion_usb.h"
#include "companion_setup.h"
#include "companion_wifi.h"
#include "companion_power.h"
#include "companion_voice.h"
#include "esp_timer.h"
#include "qrcodegen.h"
#include "companion_ble.h"
#include "companion_iphone.h"

static const char *TAG = "ai_companion";
static lv_obj_t *eyes[2], *mouth, *status_label, *brows[2], *tears[2], *setup_label, *screen_face;
static bool screen_asleep;
static int64_t dizzy_until_us;
static int64_t last_activity_us;
static uint8_t display_brightness = 85;
static uint8_t display_theme;
static uint16_t display_sleep_timeout = 30;
static companion_state_t state;
static companion_emotion_t current_emotion = COMPANION_CALM;
static bool sleeping;
static bool manual_sleep;
static bool audio_available;
static bool imu_available;
static char setup_info[128];
static char owner_setup_pin[16];
static lv_obj_t *qr_view, *qr_title_label, *qr_count_label, *qr_button, *remote_button;
static bool qr_mode;
static bool remote_mode;
static bool iphone_media_mode;
static uint8_t qr_index;
static char qr_titles[4][40], qr_urls[4][180];
static uint8_t qr_temp[qrcodegen_BUFFER_LEN_MAX], qr_code[qrcodegen_BUFFER_LEN_MAX];
static char last_iphone_notification[240], last_iphone_track[96];
static int64_t iphone_info_until_us;

static const char *emotion_label(companion_emotion_t emotion)
{
    static const char *labels[COMPANION_EMOTION_COUNT] = {
        "Calm", "Happy", "Curious", "Sleepy", "Sad",
        "Affection", "Excited", "Surprised", "Scared", "Concerned", "Shy", "Dizzy", "Flinch"
    };
    return labels[emotion < COMPANION_EMOTION_COUNT ? emotion : COMPANION_CALM];
}

static void animate_eye_height(void *object, int32_t height)
{
    lv_obj_set_height((lv_obj_t *)object, height);
}

static void animate_mouth_width(void *object, int32_t width)
{
    lv_obj_set_width((lv_obj_t *)object, width);
}

static void style_eye(lv_obj_t *eye)
{
    lv_obj_remove_style_all(eye);
    lv_obj_set_style_bg_color(eye, lv_color_hex(0xF8FBFF), 0);
    lv_obj_set_style_bg_opa(eye, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(eye, 0, 0);
    lv_obj_set_style_radius(eye, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_shadow_color(eye, lv_color_hex(0xA8D9FF), 0);
    lv_obj_set_style_shadow_width(eye, 12, 0);
    lv_obj_set_style_shadow_opa(eye, LV_OPA_30, 0);
}

static void animate_size(lv_obj_t *object, int32_t from, int32_t to,
                         uint32_t duration, lv_anim_exec_xcb_t callback)
{
    lv_anim_delete(object, callback);
    lv_anim_t animation;
    lv_anim_init(&animation);
    lv_anim_set_var(&animation, object);
    lv_anim_set_values(&animation, from, to);
    lv_anim_set_duration(&animation, duration);
    lv_anim_set_path_cb(&animation, lv_anim_path_ease_in_out);
    lv_anim_set_exec_cb(&animation, callback);
    lv_anim_start(&animation);
}

static uint32_t theme_background(void)
{
    switch (display_theme) {
        case 1: return 0xE8EDF2;
        case 2: return 0x000000;
        case 3: return 0x120D06;
        default: return 0x05070B;
    }
}

static uint32_t theme_face_color(void)
{
    switch (display_theme) {
        case 1: return 0x17212B;
        case 3: return 0xF1C27D;
        default: return 0xF8FBFF;
    }
}

static void load_display_settings(void)
{
    nvs_handle_t handle;
    if (nvs_open("companion", NVS_READONLY, &handle) != ESP_OK) return;
    uint8_t brightness = 85, theme = 0, timeout = 30;
    if (nvs_get_u8(handle, "brightness", &brightness) == ESP_OK && brightness <= 100) display_brightness = brightness;
    if (nvs_get_u8(handle, "theme", &theme) == ESP_OK && theme <= 4) display_theme = theme;
    if (nvs_get_u8(handle, "sleep_timeout", &timeout) == ESP_OK && timeout <= 240) display_sleep_timeout = timeout;
    nvs_close(handle);
}

static void apply_display_settings(void)
{
    if (bsp_display_lock(pdMS_TO_TICKS(1000)) != ESP_OK) return;
    lv_obj_set_style_bg_color(lv_screen_active(), lv_color_hex(theme_background()), 0);
    lv_obj_set_style_bg_opa(lv_screen_active(), LV_OPA_COVER, 0);
    if (eyes[0] && eyes[1]) {
        for (int i = 0; i < 2; ++i) {
            lv_obj_set_style_bg_color(eyes[i], lv_color_hex(theme_face_color()), 0);
            lv_obj_set_style_shadow_color(eyes[i], lv_color_hex(display_theme == 3 ? 0x9B6D32 : 0xA8D9FF), 0);
        }
    }
    if (mouth) lv_obj_set_style_border_color(mouth, lv_color_hex(theme_face_color()), 0);
    if (status_label) lv_obj_set_style_text_color(status_label, lv_color_hex(display_theme == 1 ? 0x263442 : theme_face_color()), 0);
    if (setup_label) lv_obj_set_style_text_color(setup_label, lv_color_hex(display_theme == 1 ? 0x536272 : 0x7F91A5), 0);
    bsp_display_unlock();
    if (!screen_asleep) (void)bsp_display_brightness_set(display_brightness);
}

static void render_emotion(companion_emotion_t emotion)
{
    if (emotion >= COMPANION_EMOTION_COUNT) emotion = COMPANION_CALM;
    current_emotion = emotion;
    const bool sad = emotion == COMPANION_SAD;
    const bool sleepy = emotion == COMPANION_SLEEPY;
    const bool excited = emotion == COMPANION_EXCITED;
    const bool curious = emotion == COMPANION_CURIOUS || emotion == COMPANION_SURPRISED || emotion == COMPANION_SCARED;
    const bool dizzy = emotion == COMPANION_DIZZY;
    const bool flinch = emotion == COMPANION_FLINCH;
    const bool scared = emotion == COMPANION_SCARED;
    int eye_width = 58, eye_height = 78, mouth_width = 27, mouth_height = 8;
    int mouth_top = 0;

    switch (emotion) {
        case COMPANION_HAPPY: eye_height = 56; mouth_width = 40; mouth_height = 15; break;
        case COMPANION_CURIOUS: eye_width = 63; eye_height = 88; mouth_width = 15; mouth_height = 18; break;
        case COMPANION_SLEEPY: eye_height = 6; mouth_width = 20; mouth_height = 4; break;
        case COMPANION_SAD: eye_width = 55; eye_height = 61; mouth_width = 37; mouth_height = 17; mouth_top = 8; break;
        case COMPANION_AFFECTIONATE: eye_width = 61; eye_height = 70; mouth_width = 36; mouth_height = 13; break;
        case COMPANION_EXCITED: eye_width = 68; eye_height = 96; mouth_width = 24; mouth_height = 25; break;
        case COMPANION_SURPRISED: eye_width = 66; eye_height = 100; mouth_width = 15; mouth_height = 22; break;
        case COMPANION_SCARED: eye_width = 68; eye_height = 91; mouth_width = 17; mouth_height = 23; break;
        case COMPANION_CONCERNED: eye_width = 57; eye_height = 70; mouth_width = 29; mouth_height = 8; break;
        case COMPANION_SHY: eye_width = 58; eye_height = 60; mouth_width = 16; mouth_height = 7; break;
        case COMPANION_DIZZY: eye_width = 54; eye_height = 48; mouth_width = 34; mouth_height = 12; break;
        case COMPANION_FLINCH: eye_width = 58; eye_height = 4; mouth_width = 11; mouth_height = 4; break;
        default: break;
    }

    for (int i = 0; i < 2; ++i) {
        int old_height = lv_obj_get_height(eyes[i]);
        lv_obj_set_width(eyes[i], eye_width);
        animate_size(eyes[i], old_height > 0 ? old_height : 78, eye_height, 260, animate_eye_height);
        lv_obj_set_x(eyes[i], i == 0 ? 150 - eye_width / 2 : 316 - eye_width / 2);
        lv_obj_set_y(eyes[i], sad ? 156 : 164);
        lv_obj_set_style_bg_color(eyes[i], sad ? lv_color_hex(0xDCEEFF) : lv_color_hex(theme_face_color()), 0);
        lv_obj_set_style_shadow_color(eyes[i], sad ? lv_color_hex(0x6BB7E8) : lv_color_hex(0xA8D9FF), 0);
        lv_obj_set_style_shadow_width(eyes[i], sad ? 20 : 12, 0);
        lv_obj_set_style_bg_opa(eyes[i], (sleepy || flinch) ? LV_OPA_80 : LV_OPA_COVER, 0);
        lv_obj_set_style_radius(eyes[i], (sleepy || flinch) ? 8 : LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_transform_rotation(eyes[i], sad ? (i == 0 ? -65 : 65) : (dizzy ? (i == 0 ? 18 : -18) : (scared ? (i == 0 ? -5 : 5) : 0)), 0);
        lv_obj_clear_flag(tears[i], LV_OBJ_FLAG_HIDDEN);
        if (sad) lv_obj_set_y(tears[i], 218 + (i ? 5 : 0));
        else lv_obj_add_flag(tears[i], LV_OBJ_FLAG_HIDDEN);
        if (sad) {
            lv_obj_clear_flag(brows[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_style_bg_color(brows[i], lv_color_hex(0xCDE7FA), 0);
            lv_obj_set_style_transform_rotation(brows[i], i == 0 ? 150 : -150, 0);
        } else {
            lv_obj_add_flag(brows[i], LV_OBJ_FLAG_HIDDEN);
        }
    }

    int old_width = lv_obj_get_width(mouth);
    animate_size(mouth, old_width > 0 ? old_width : 27, mouth_width, 220, animate_mouth_width);
    lv_obj_set_height(mouth, mouth_height);
    lv_obj_set_y(mouth, 280 + mouth_top);
    lv_obj_set_style_bg_opa(mouth, excited || curious ? LV_OPA_20 : LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_color(mouth, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_color(mouth, sad ? lv_color_hex(0xC6E6FF) : lv_color_hex(theme_face_color()), 0);
    lv_obj_set_style_border_width(mouth, sleepy ? 0 : 3, 0);
    lv_obj_set_style_border_side(mouth, sad || emotion == COMPANION_CONCERNED ? LV_BORDER_SIDE_TOP : LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_radius(mouth, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_transform_rotation(mouth, emotion == COMPANION_SHY ? -40 : (dizzy ? 12 : 0), 0);
    lv_label_set_text(status_label, emotion_label(emotion));
    companion_usb_emit_emotion(emotion_label(emotion));
}

static void set_emotion(companion_emotion_t emotion, bool sound)
{
    if (bsp_display_lock(pdMS_TO_TICKS(1000)) == ESP_OK) {
        render_emotion(emotion);
        bsp_display_unlock();
    }
    if (sound && audio_available) {
        esp_err_t err = companion_audio_play(emotion, false);
        if (err != ESP_OK && err != ESP_ERR_TIMEOUT) ESP_LOGW(TAG, "Audio play: %s", esp_err_to_name(err));
    }
}

static void set_screen_sleep(bool asleep)
{
    if (screen_asleep == asleep || !screen_face) return;
    if (bsp_display_lock(pdMS_TO_TICKS(1000)) != ESP_OK) return;
    screen_asleep = asleep;
    lv_obj_set_style_bg_color(lv_screen_active(), lv_color_hex(asleep ? 0x000000 : theme_background()), 0);
    if (asleep) {
        /* The CO5300 AMOLED has no LED backlight: a fully black frame blanks its pixels. */
        lv_obj_add_flag(screen_face, LV_OBJ_FLAG_HIDDEN);
        if (status_label) lv_obj_add_flag(status_label, LV_OBJ_FLAG_HIDDEN);
        if (setup_label) lv_obj_add_flag(setup_label, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_clear_flag(screen_face, LV_OBJ_FLAG_HIDDEN);
        if (status_label) lv_obj_clear_flag(status_label, LV_OBJ_FLAG_HIDDEN);
        if (setup_label) lv_obj_clear_flag(setup_label, LV_OBJ_FLAG_HIDDEN);
    }
    bsp_display_unlock();
    (void)bsp_display_brightness_set(asleep ? 0 : display_brightness);
}

static void save_state(void)
{
    nvs_handle_t handle;
    if (nvs_open("companion", NVS_READWRITE, &handle) != ESP_OK) return;
    nvs_set_blob(handle, "emotion_state", &state, sizeof(state));
    nvs_commit(handle);
    nvs_close(handle);
}

static void refresh_personality(void)
{
    nvs_handle_t handle;
    if (nvs_open("companion", NVS_READONLY, &handle) != ESP_OK) return;
    uint8_t temperament = 0, initiative = 0, quirk = 0;
    if (nvs_get_u8(handle, "temperament", &temperament) == ESP_OK) state.temperament = temperament <= 4 ? temperament : 0;
    if (nvs_get_u8(handle, "initiative", &initiative) == ESP_OK) state.initiative = initiative <= 2 ? initiative : 0;
    if (nvs_get_u8(handle, "quirk", &quirk) == ESP_OK) state.quirk = quirk <= 4 ? quirk : 0;
    nvs_close(handle);
    if (audio_available) companion_audio_set_temperament(state.temperament);
}

static void load_state(void)
{
    companion_emotion_init(&state);
    nvs_handle_t handle;
    if (nvs_open("companion", NVS_READONLY, &handle) != ESP_OK) return;
    size_t size = sizeof(state);
    if (nvs_get_blob(handle, "emotion_state", &state, &size) != ESP_OK || size != sizeof(state)) {
        companion_emotion_init(&state);
    }
    uint8_t temperament = 0, initiative = 0, quirk = 0;
    if (nvs_get_u8(handle, "temperament", &temperament) == ESP_OK) state.temperament = temperament <= 4 ? temperament : 0;
    if (nvs_get_u8(handle, "initiative", &initiative) == ESP_OK) state.initiative = initiative <= 2 ? initiative : 0;
    if (nvs_get_u8(handle, "quirk", &quirk) == ESP_OK) state.quirk = quirk <= 4 ? quirk : 0;
    nvs_close(handle);
}

static companion_emotion_t parse_emotion(const char *value)
{
    if (!value) return COMPANION_CALM;
    if (!strcmp(value, "happy")) return COMPANION_HAPPY;
    if (!strcmp(value, "curious")) return COMPANION_CURIOUS;
    if (!strcmp(value, "sleepy")) return COMPANION_SLEEPY;
    if (!strcmp(value, "sad")) return COMPANION_SAD;
    if (!strcmp(value, "affectionate")) return COMPANION_AFFECTIONATE;
    if (!strcmp(value, "excited")) return COMPANION_EXCITED;
    if (!strcmp(value, "surprised")) return COMPANION_SURPRISED;
    if (!strcmp(value, "scared")) return COMPANION_SCARED;
    if (!strcmp(value, "concerned")) return COMPANION_CONCERNED;
    if (!strcmp(value, "shy")) return COMPANION_SHY;
    if (!strcmp(value, "dizzy")) return COMPANION_DIZZY;
    if (!strcmp(value, "flinch")) return COMPANION_FLINCH;
    return COMPANION_CALM;
}

static void apply_cloud_intent(const char *json_text)
{
    cJSON *json = cJSON_Parse(json_text);
    if (!json) return;
    const cJSON *addressed = cJSON_GetObjectItemCaseSensitive(json, "addressed_to_companion");
    const cJSON *understood = cJSON_GetObjectItemCaseSensitive(json, "understood");
    const cJSON *action = cJSON_GetObjectItemCaseSensitive(json, "action");
    const cJSON *emotion = cJSON_GetObjectItemCaseSensitive(json, "emotion");
    const cJSON *confidence = cJSON_GetObjectItemCaseSensitive(json, "confidence");

    if (!cJSON_IsTrue(addressed) || !cJSON_IsString(action) || !cJSON_IsString(emotion)) {
        cJSON_Delete(json);
        return;
    }

    const bool needs_clarification = !cJSON_IsTrue(understood) ||
        (cJSON_IsNumber(confidence) && confidence->valuedouble < 0.35);
    if (needs_clarification || !strcmp(action->valuestring, "clarify")) {
        set_emotion(COMPANION_CURIOUS, true);
        cJSON_Delete(json);
        return;
    }

    const char *kind = NULL;
    companion_emotion_t next = parse_emotion(emotion->valuestring);
    const char *verb = action->valuestring;

    if (!strcmp(verb, "idle")) {
        cJSON_Delete(json);
        return;
    } else if (!strcmp(verb, "sleep")) {
        sleeping = true;
        manual_sleep = true;
        next = COMPANION_SLEEPY;
    } else if (!strcmp(verb, "wake")) {
        sleeping = false;
        manual_sleep = false;
        kind = "call";
        next = COMPANION_HAPPY;
    } else if (!strcmp(verb, "pet") || !strcmp(verb, "comfort")) {
        kind = "pet";
        if (!strcmp(verb, "comfort") && next == COMPANION_CALM) next = COMPANION_AFFECTIONATE;
    } else if (!strcmp(verb, "greet") || !strcmp(verb, "acknowledge") ||
               !strcmp(verb, "attend") || !strcmp(verb, "thank")) {
        kind = "call";
        if (!strcmp(verb, "thank") && next == COMPANION_CALM) next = COMPANION_AFFECTIONATE;
    } else if (!strcmp(verb, "surprise")) {
        next = COMPANION_SURPRISED;
    } else if (!strcmp(verb, "express_joy")) {
        next = COMPANION_HAPPY;
    } else if (!strcmp(verb, "express_curiosity")) {
        next = COMPANION_CURIOUS;
    }

    if (kind) companion_emotion_interaction(&state, kind);
    if (strcmp(verb, "sleep") != 0) {
        sleeping = false;
        manual_sleep = false;
    }
    set_emotion(next, true);
    save_state();
    cJSON_Delete(json);
}


static void on_usb_command(const char *command)
{
    if (!command) return;
    last_activity_us = esp_timer_get_time();
    if (screen_asleep) set_screen_sleep(false);
    if (!strcmp(command, "mood")) {
        companion_usb_emit_emotion(emotion_label(current_emotion));
    } else if (!strcmp(command, "profile")) {
        refresh_personality();
        load_display_settings();
        apply_display_settings();
        save_state();
    } else if (!strcmp(command, "sleep")) {
        sleeping = true;
        manual_sleep = true;
        set_emotion(COMPANION_SLEEPY, true);
    } else if (!strcmp(command, "wake")) {
        sleeping = false;
        manual_sleep = false;
        set_screen_sleep(false);
        set_emotion(COMPANION_HAPPY, true);
    } else if (!strcmp(command, "pet")) {
        sleeping = false;
        manual_sleep = false;
        companion_emotion_interaction(&state, "pet");
        set_emotion(COMPANION_HAPPY, false);
        if (audio_available) companion_audio_play_sound(VOICE_PURR, false);
        save_state();
    } else if (!strcmp(command, "call")) {
        sleeping = false;
        manual_sleep = false;
        companion_emotion_interaction(&state, "call");
        set_emotion(COMPANION_CURIOUS, false);
        if (audio_available) companion_audio_play_sound(VOICE_TICK, false);
        save_state();
    } else if (!strncmp(command, "emotion ", 8)) {
        const companion_emotion_t emotion = parse_emotion(command + 8);
        set_emotion(emotion, true);
    } else if (!strncmp(command, "sound ", 6)) {
        const char *sound = command + 6;
        int cue = VOICE_HM;
        if (!strcmp(sound, "purr")) cue = VOICE_PURR;
        else if (!strcmp(sound, "growl")) cue = VOICE_GROWL;
        else if (!strcmp(sound, "nom")) cue = VOICE_NOM;
        else if (!strcmp(sound, "dizzy")) cue = VOICE_DIZZY;
        else if (!strcmp(sound, "surprise")) cue = VOICE_OH;
        else if (!strcmp(sound, "hmph")) cue = VOICE_HMPH;
        if (audio_available) companion_audio_play_sound(cue, true);
    } else if (!strcmp(command, "wifi-reset")) {
        (void)companion_wifi_reset();
    }
}


static void qr_load_slots(void)
{
    memset(qr_titles, 0, sizeof(qr_titles));
    memset(qr_urls, 0, sizeof(qr_urls));
    nvs_handle_t handle;
    if (nvs_open("companion", NVS_READONLY, &handle) != ESP_OK) return;
    const char *keys[4] = {"qr0", "qr1", "qr2", "qr3"};
    for (int i = 0; i < 4; ++i) {
        char value[220] = {0};
        size_t length = sizeof(value);
        if (nvs_get_str(handle, keys[i], value, &length) != ESP_OK) continue;
        char *separator = strchr(value, '|');
        if (separator) {
            *separator++ = '\0';
            snprintf(qr_titles[i], sizeof(qr_titles[i]), "%s", value);
            snprintf(qr_urls[i], sizeof(qr_urls[i]), "%s", separator);
        } else if (value[0]) {
            snprintf(qr_urls[i], sizeof(qr_urls[i]), "%s", value);
            snprintf(qr_titles[i], sizeof(qr_titles[i]), "QR %d", i + 1);
        }
    }
    nvs_close(handle);
}

static bool qr_prepare(void)
{
    if (!qr_urls[qr_index][0]) return false;
    return qrcodegen_encodeText(qr_urls[qr_index], qr_temp, qr_code,
        qrcodegen_Ecc_MEDIUM, 1, 10, qrcodegen_Mask_AUTO, true);
}

static void qr_draw_event(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_DRAW_MAIN) return;
    lv_obj_t *obj = lv_event_get_target(event);
    lv_layer_t *layer = lv_event_get_layer(event);
    lv_area_t bounds;
    lv_obj_get_coords(obj, &bounds);
    lv_draw_rect_dsc_t rect;
    lv_draw_rect_dsc_init(&rect);
    rect.bg_opa = LV_OPA_COVER;
    rect.bg_color = lv_color_white();
    rect.radius = 8;
    lv_draw_rect(layer, &rect, &bounds);
    if (!qr_prepare()) return;
    const int size = qrcodegen_getSize(qr_code);
    const int module = 5;
    const int total = (size + 8) * module;
    const int left = bounds.x1 + (lv_area_get_width(&bounds) - total) / 2 + 4 * module;
    const int top = bounds.y1 + (lv_area_get_height(&bounds) - total) / 2 + 4 * module;
    rect.bg_color = lv_color_black();
    rect.radius = 0;
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            if (!qrcodegen_getModule(qr_code, x, y)) continue;
            lv_area_t cell = {
                .x1 = left + x * module, .y1 = top + y * module,
                .x2 = left + (x + 1) * module - 1, .y2 = top + (y + 1) * module - 1
            };
            lv_draw_rect(layer, &rect, &cell);
        }
    }
}

static void qr_refresh(void)
{
    if (!qr_mode || !qr_view) return;
    lv_label_set_text(qr_title_label, qr_titles[qr_index][0] ? qr_titles[qr_index] :
                      (qr_urls[qr_index][0] ? "Quick QR" : "Нет сохранённой ссылки"));
    char count[24];
    snprintf(count, sizeof(count), "%u / 4", (unsigned)(qr_index + 1));
    lv_label_set_text(qr_count_label, count);
    lv_obj_invalidate(qr_view);
}

static void qr_show(bool show)
{
    if (!qr_view || !screen_face) return;
    qr_mode = show;
    if (show) {
        qr_load_slots();
        lv_obj_add_flag(screen_face, LV_OBJ_FLAG_HIDDEN);
        if (status_label) lv_obj_add_flag(status_label, LV_OBJ_FLAG_HIDDEN);
        if (setup_label) lv_obj_add_flag(setup_label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(qr_view, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(qr_title_label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(qr_count_label, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(qr_button, "CLOSE");
        qr_refresh();
    } else {
        lv_obj_add_flag(qr_view, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(qr_title_label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(qr_count_label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(screen_face, LV_OBJ_FLAG_HIDDEN);
        if (status_label) lv_obj_clear_flag(status_label, LV_OBJ_FLAG_HIDDEN);
        if (setup_label) lv_obj_clear_flag(setup_label, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(qr_button, "QR");
        render_emotion(current_emotion);
    }
}

static void qr_button_event(lv_event_t *event)
{
    if (lv_event_get_code(event) == LV_EVENT_CLICKED) qr_show(!qr_mode);
}

static void qr_gesture_event(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_GESTURE || !qr_mode) return;
    const lv_dir_t direction = lv_indev_get_gesture_dir(lv_indev_active());
    if (direction == LV_DIR_LEFT) qr_index = (uint8_t)((qr_index + 1U) % 4U);
    else if (direction == LV_DIR_RIGHT) qr_index = (uint8_t)((qr_index + 3U) % 4U);
    else return;
    qr_refresh();
}

static void on_remote_gesture(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_GESTURE || qr_mode) return;
    const lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_active());
    esp_err_t err = ESP_OK;
    if (iphone_media_mode) {
        /* iPhone AMS mode: swipe left/right for tracks, up for play/pause, down for volume up. */
        if (dir == LV_DIR_LEFT) err = companion_iphone_media_command(COMPANION_IPHONE_PREVIOUS_TRACK);
        else if (dir == LV_DIR_RIGHT) err = companion_iphone_media_command(COMPANION_IPHONE_NEXT_TRACK);
        else if (dir == LV_DIR_TOP) err = companion_iphone_media_command(COMPANION_IPHONE_TOGGLE_PLAY_PAUSE);
        else if (dir == LV_DIR_BOTTOM) err = companion_iphone_media_command(COMPANION_IPHONE_VOLUME_UP);
        else return;
    } else if (remote_mode) {
        /* In mouse mode, swipe moves the host cursor and vertical swipe scrolls. */
        if (dir == LV_DIR_LEFT) err = companion_ble_send_mouse(-36, 0, 0, 0);
        else if (dir == LV_DIR_RIGHT) err = companion_ble_send_mouse(36, 0, 0, 0);
        else if (dir == LV_DIR_TOP) err = companion_ble_send_mouse(0, 0, 2, 0);
        else if (dir == LV_DIR_BOTTOM) err = companion_ble_send_mouse(0, 0, -2, 0);
        else return;
    } else {
        companion_ble_key_t key;
        if (dir == LV_DIR_LEFT) key = COMPANION_BLE_PRESENTATION_NEXT;
        else if (dir == LV_DIR_RIGHT) key = COMPANION_BLE_PRESENTATION_PREVIOUS;
        else if (dir == LV_DIR_TOP) key = COMPANION_BLE_PLAY_PAUSE;
        else if (dir == LV_DIR_BOTTOM) key = COMPANION_BLE_MUTE;
        else return;
        err = companion_ble_send_key(key);
    }
    if (err != ESP_OK) ESP_LOGW(TAG, "BLE remote unavailable: %s", esp_err_to_name(err));
}

static void remote_button_event(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    if (!remote_mode && !iphone_media_mode) {
        remote_mode = true;
        lv_label_set_text(remote_button, "MOUSE");
        lv_label_set_text(status_label, "Mouse mode · swipe to move · tap to click");
    } else if (remote_mode) {
        remote_mode = false;
        iphone_media_mode = true;
        lv_label_set_text(remote_button, "IPHONE");
        lv_label_set_text(status_label, "iPhone mode · swipe for music controls");
    } else {
        iphone_media_mode = false;
        lv_label_set_text(remote_button, "REMOTE");
        lv_label_set_text(status_label, "Presentation mode · swipe to control");
    }
}

static void on_face_long_press(lv_event_t *event)
{
    (void)event;
    if (remote_mode || iphone_media_mode) {
        remote_mode = false;
        iphone_media_mode = false;
        lv_label_set_text(remote_button, "REMOTE");
        lv_label_set_text(status_label, "Remote mode disabled");
        return;
    }
    lv_label_set_text(status_label, "Resetting Wi-Fi…");
    companion_wifi_reset();
}

static void on_face_tap(lv_event_t *event)
{
    (void)event;
    if (qr_mode) return;
    if (iphone_media_mode) {
        esp_err_t err = companion_iphone_media_command(COMPANION_IPHONE_VOLUME_DOWN);
        if (err != ESP_OK) ESP_LOGW(TAG, "iPhone volume command unavailable: %s", esp_err_to_name(err));
        return;
    }
    if (remote_mode) {
        esp_err_t err = companion_ble_mouse_click(false);
        if (err != ESP_OK) ESP_LOGW(TAG, "BLE mouse click unavailable: %s", esp_err_to_name(err));
        return;
    }
    if (sleeping) sleeping = false;
    manual_sleep = false;
    last_activity_us = esp_timer_get_time();
    set_screen_sleep(false);
    companion_emotion_interaction(&state, "pet");
    companion_usb_emit_touch("tap");
    render_emotion(COMPANION_HAPPY);
    if (audio_available) companion_audio_play(COMPANION_HAPPY, false);
    save_state();
}

static void blink_timer(lv_timer_t *timer)
{
    (void)timer;
    if (sleeping || screen_asleep || !eyes[0]) return;
    if ((esp_random() % 100U) > 72U) return;
    for (int i = 0; i < 2; ++i) {
        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, eyes[i]);
        lv_anim_set_values(&a, lv_obj_get_height(eyes[i]), 5);
        lv_anim_set_duration(&a, 85);
        lv_anim_set_reverse_duration(&a, 115);
        lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
        lv_anim_set_exec_cb(&a, animate_eye_height);
        lv_anim_start(&a);
    }
}

static void autonomous_timer(lv_timer_t *timer)
{
    (void)timer;
    refresh_personality();
    companion_iphone_poll();
    const int64_t now_us = esp_timer_get_time();

    /* Surface iPhone ANCS notifications and AMS track metadata on the AMOLED. */
    if (!qr_mode && !screen_asleep && status_label) {
        char notification[240] = {0}, title[96] = {0}, artist[96] = {0}, player[48] = {0};
        uint8_t playback = 0;
        companion_iphone_get_notification(notification, sizeof(notification));
        companion_iphone_get_media(title, sizeof(title), artist, sizeof(artist), player, sizeof(player), &playback);
        if (notification[0] && strcmp(notification, last_iphone_notification) != 0) {
            snprintf(last_iphone_notification, sizeof(last_iphone_notification), "%s", notification);
            lv_label_set_text(status_label, notification);
            lv_label_set_long_mode(status_label, LV_LABEL_LONG_DOT);
            lv_obj_set_width(status_label, 420);
            lv_obj_set_style_text_align(status_label, LV_TEXT_ALIGN_CENTER, 0);
            iphone_info_until_us = now_us + 7000000LL;
        } else if (!notification[0] && last_iphone_notification[0]) {
            last_iphone_notification[0] = 0;
            iphone_info_until_us = now_us;
        }
        if (!notification[0] && title[0]) {
            char track[96];
            snprintf(track, sizeof(track), "%s", title);
            if (strcmp(track, last_iphone_track) != 0) {
                snprintf(last_iphone_track, sizeof(last_iphone_track), "%s", track);
                char line[220];
                snprintf(line, sizeof(line), "%s%s%s", title, artist[0] ? " — " : "", artist);
                lv_label_set_text(status_label, line);
                lv_label_set_long_mode(status_label, LV_LABEL_LONG_DOT);
                lv_obj_set_width(status_label, 420);
                lv_obj_set_style_text_align(status_label, LV_TEXT_ALIGN_CENTER, 0);
                iphone_info_until_us = now_us + 5000000LL;
            }
        } else if (!title[0]) {
            last_iphone_track[0] = 0;
        }
        if (iphone_info_until_us && now_us >= iphone_info_until_us && !notification[0]) {
            iphone_info_until_us = 0;
            lv_label_set_text(status_label, emotion_label(current_emotion));
            lv_obj_set_width(status_label, LV_SIZE_CONTENT);
        }
    }
    if (last_activity_us == 0) last_activity_us = now_us;
    if (!screen_asleep && display_sleep_timeout > 0 &&
        now_us - last_activity_us >= (int64_t)display_sleep_timeout * 1000000LL) {
        set_screen_sleep(true);
    }
    companion_emotion_tick(&state, 8, sleeping);
    if (!sleeping) {
        const uint32_t chance = state.initiative == 1 ? 42U : (state.initiative == 2 ? 12U : 25U);
        if ((esp_random() % 100U) >= chance) { save_state(); return; }
    }
    if (!sleeping) {
        const time_t now = time(NULL);
        struct tm local = {0};
        localtime_r(&now, &local);
        const uint8_t hour = now > 1700000000 ? (uint8_t)local.tm_hour : 12;
        companion_emotion_t next = companion_emotion_choose(&state, hour, esp_random(), current_emotion);
        if (next == COMPANION_SLEEPY) { sleeping = true; manual_sleep = false; }
        if (next != current_emotion) render_emotion(next);
    }
    save_state();
}

static void voice_task(void *arg)
{
    (void)arg;
    const size_t max_samples = 16000U * 6U;
    int16_t *pcm = heap_caps_malloc(max_samples * sizeof(int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!pcm) pcm = heap_caps_malloc(max_samples * sizeof(int16_t), MALLOC_CAP_8BIT);
    if (!pcm) {
        ESP_LOGE(TAG, "Unable to allocate voice buffer");
        vTaskDelete(NULL);
        return;
    }
    while (true) {
        if (!audio_available) { vTaskDelay(pdMS_TO_TICKS(1000)); continue; }
        size_t samples = 0;
        esp_err_t err = companion_audio_capture_utterance(pcm, max_samples, &samples, 6000);
        const uint32_t audio_events = companion_audio_take_events();
        if (audio_events & COMPANION_AUDIO_BLOW) {
            companion_usb_emit_touch("blow");
            set_emotion(COMPANION_FLINCH, false);
            if (audio_available) companion_audio_play_sound(VOICE_POKE, false);
            vTaskDelay(pdMS_TO_TICKS(850));
            if (!sleeping) set_emotion(COMPANION_CURIOUS, false);
            continue;
        }
        if (audio_events & COMPANION_AUDIO_CLAP) {
            companion_usb_emit_touch("clap");
            set_emotion(COMPANION_SCARED, false);
            if (audio_available) companion_audio_play_sound(VOICE_OH, false);
            continue;
        }
        if (err == ESP_OK && samples > 0) {
            if (!companion_wifi_is_connected()) {
                ESP_LOGI(TAG, "Speech detected offline; local touch and emotion engine remain active");
                continue;
            }
            char transcript[1200] = {0};
            err = companion_cloud_transcribe(pcm, samples, transcript, sizeof(transcript));
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "Speech recognition unavailable: %s", esp_err_to_name(err));
                continue;
            }
            ESP_LOGI(TAG, "Recognized owner speech (%u chars)", (unsigned)strlen(transcript));
            char intent_json[4096] = {0};
            err = companion_cloud_interpret(transcript, intent_json, sizeof(intent_json));
            if (err == ESP_OK) apply_cloud_intent(intent_json);
            else ESP_LOGW(TAG, "Cloud intent unavailable; local behavior continues: %s", esp_err_to_name(err));
        } else if (err != ESP_ERR_NOT_FOUND && err != ESP_ERR_TIMEOUT) {
            ESP_LOGW(TAG, "Microphone read: %s", esp_err_to_name(err));
            vTaskDelay(pdMS_TO_TICKS(300));
        }
    }
}

static void imu_task(void *arg)
{
    (void)arg;
    while (true) {
        if (imu_available) {
            const uint32_t events = companion_imu_poll();
            if (events & COMPANION_MOTION_FACE_DOWN) {
                companion_usb_emit_touch("face_down");
                sleeping = true;
                manual_sleep = false;
                set_emotion(COMPANION_SLEEPY, false);
                set_screen_sleep(true);
            } else if (events & COMPANION_MOTION_FACE_UP) {
                companion_usb_emit_touch("face_up");
                sleeping = false;
                manual_sleep = false;
                set_screen_sleep(false);
                set_emotion(COMPANION_HAPPY, true);
            } else if (events & COMPANION_MOTION_SHAKE) {
                companion_usb_emit_touch("shake");
                dizzy_until_us = esp_timer_get_time() + 2600000LL;
                set_emotion(COMPANION_DIZZY, true);
            } else if (events & COMPANION_MOTION_FALL) {
                set_emotion(COMPANION_SURPRISED, false);
            } else if ((events & (COMPANION_MOTION_UPSIDE_DOWN | COMPANION_MOTION_UPRIGHT)) &&
                       current_emotion == COMPANION_CALM) {
                set_emotion(COMPANION_CURIOUS, false);
            }
            if (dizzy_until_us && esp_timer_get_time() >= dizzy_until_us &&
                current_emotion == COMPANION_DIZZY && !sleeping) {
                dizzy_until_us = 0;
                set_emotion(COMPANION_CURIOUS, false);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

static void power_task(void *arg)
{
    (void)arg;
    esp_err_t err = companion_power_init();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "AXP2101 unavailable; charging reactions disabled: %s", esp_err_to_name(err));
        vTaskDelete(NULL);
        return;
    }
    while (true) {
        const uint32_t events = companion_power_poll();
        if (events & COMPANION_POWER_CHARGER_CONNECTED) {
            companion_usb_emit_touch("charge");
            companion_emotion_interaction(&state, "charge");
            set_emotion(COMPANION_HAPPY, false);
            if (audio_available) companion_audio_play_sound(VOICE_OH, false);
            save_state();
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

static void network_setup_task(void *arg)
{
    (void)arg;
    while (!companion_wifi_is_connected()) vTaskDelay(pdMS_TO_TICKS(500));
    uint32_t local_pin = 10000000U + (esp_random() % 90000000U);
    snprintf(owner_setup_pin, sizeof(owner_setup_pin), "%08lu", (unsigned long)local_pin);
    nvs_handle_t config_handle;
    if (nvs_open("companion", NVS_READWRITE, &config_handle) == ESP_OK) {
        nvs_set_str(config_handle, "setup_pin", owner_setup_pin);
        nvs_commit(config_handle);
        nvs_close(config_handle);
    }
    if (companion_setup_server_start() == ESP_OK) {
        esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        esp_netif_ip_info_t ip = {0};
        char line[112] = "Owner portal online";
        if (netif && esp_netif_get_ip_info(netif, &ip) == ESP_OK) {
            snprintf(line, sizeof(line), "http://" IPSTR "  PIN:%s", IP2STR(&ip.ip), owner_setup_pin);
        }
        ESP_LOGI(TAG, "Owner setup portal ready. PIN is displayed on the device.");
        if (bsp_display_lock(pdMS_TO_TICKS(1000)) == ESP_OK) {
            lv_label_set_text(setup_label, line);
            bsp_display_unlock();
        }
    }
    vTaskDelete(NULL);
}

void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
    load_state();
    load_display_settings();

    if (!bsp_display_start()) {
        ESP_LOGE(TAG, "Display initialization failed");
        return;
    }
    bsp_display_backlight_on();
    (void)bsp_display_brightness_set(display_brightness);
    last_activity_us = esp_timer_get_time();
    ESP_ERROR_CHECK(bsp_display_lock(-1));
    lv_obj_t *screen = lv_screen_active();
    lv_obj_add_flag(screen, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(screen, on_face_tap, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(screen, on_remote_gesture, LV_EVENT_GESTURE, NULL);
    lv_obj_set_style_bg_color(screen, lv_color_hex(theme_background()), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

    lv_obj_t *face = lv_obj_create(screen);
    screen_face = face;
    lv_obj_remove_style_all(face);
    lv_obj_set_size(face, 466, 330);
    lv_obj_align(face, LV_ALIGN_CENTER, 0, -25);
    lv_obj_add_flag(face, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(face, on_face_tap, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(face, on_face_long_press, LV_EVENT_LONG_PRESSED, NULL);

    for (int i = 0; i < 2; ++i) {
        eyes[i] = lv_obj_create(face);
        style_eye(eyes[i]);
        lv_obj_set_size(eyes[i], 58, 78);
        lv_obj_set_pos(eyes[i], i == 0 ? 121 : 287, 112);
        lv_obj_add_flag(eyes[i], LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(eyes[i], on_face_tap, LV_EVENT_CLICKED, NULL);
        lv_obj_add_event_cb(eyes[i], on_face_long_press, LV_EVENT_LONG_PRESSED, NULL);
        brows[i] = lv_obj_create(face);
        lv_obj_remove_style_all(brows[i]);
        lv_obj_set_size(brows[i], 48, 7);
        lv_obj_set_pos(brows[i], i == 0 ? 126 : 292, 94);
        lv_obj_set_style_bg_color(brows[i], lv_color_hex(0xDCEEFF), 0);
        lv_obj_set_style_bg_opa(brows[i], LV_OPA_COVER, 0);
        lv_obj_set_style_radius(brows[i], LV_RADIUS_CIRCLE, 0);
        lv_obj_add_flag(brows[i], LV_OBJ_FLAG_HIDDEN);
        tears[i] = lv_obj_create(face);
        lv_obj_remove_style_all(tears[i]);
        lv_obj_set_size(tears[i], i == 0 ? 11 : 9, i == 0 ? 20 : 16);
        lv_obj_set_pos(tears[i], i == 0 ? 145 : 310, 218);
        lv_obj_set_style_bg_color(tears[i], lv_color_hex(0x66C7FF), 0);
        lv_obj_set_style_bg_opa(tears[i], LV_OPA_COVER, 0);
        lv_obj_set_style_radius(tears[i], LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_shadow_color(tears[i], lv_color_hex(0x58BFFF), 0);
        lv_obj_set_style_shadow_width(tears[i], 12, 0);
        lv_obj_add_flag(tears[i], LV_OBJ_FLAG_HIDDEN);
    }

    mouth = lv_obj_create(face);
    lv_obj_remove_style_all(mouth);
    lv_obj_set_size(mouth, 27, 8);
    lv_obj_set_pos(mouth, 219, 280);
    lv_obj_set_style_border_color(mouth, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_width(mouth, 3, 0);
    lv_obj_set_style_border_side(mouth, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_radius(mouth, LV_RADIUS_CIRCLE, 0);
    lv_obj_add_flag(mouth, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(mouth, on_face_tap, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(mouth, on_face_long_press, LV_EVENT_LONG_PRESSED, NULL);

    status_label = lv_label_create(screen);
    lv_obj_set_style_text_color(status_label, lv_color_hex(0xD9E8F6), 0);
    lv_obj_align(status_label, LV_ALIGN_CENTER, 0, 166);
    setup_label = lv_label_create(screen);
    lv_obj_set_style_text_color(setup_label, lv_color_hex(0x7F91A5), 0);
    lv_obj_align(setup_label, LV_ALIGN_BOTTOM_MID, 0, -18);

    qr_view = lv_obj_create(screen);
    lv_obj_remove_style_all(qr_view);
    lv_obj_set_size(qr_view, 360, 360);
    lv_obj_center(qr_view);
    lv_obj_add_flag(qr_view, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(qr_view, qr_draw_event, LV_EVENT_DRAW_MAIN, NULL);
    lv_obj_add_event_cb(qr_view, qr_gesture_event, LV_EVENT_GESTURE, NULL);
    lv_obj_add_flag(qr_view, LV_OBJ_FLAG_GESTURE_BUBBLE);

    qr_title_label = lv_label_create(screen);
    lv_obj_set_style_text_color(qr_title_label, lv_color_hex(0xEAF4FF), 0);
    lv_obj_align(qr_title_label, LV_ALIGN_TOP_MID, 0, 18);
    lv_obj_add_flag(qr_title_label, LV_OBJ_FLAG_HIDDEN);
    qr_count_label = lv_label_create(screen);
    lv_obj_set_style_text_color(qr_count_label, lv_color_hex(0x91A6BB), 0);
    lv_obj_align(qr_count_label, LV_ALIGN_BOTTOM_MID, 0, -48);
    lv_obj_add_flag(qr_count_label, LV_OBJ_FLAG_HIDDEN);
    remote_button = lv_label_create(screen);
    lv_label_set_text(remote_button, "REMOTE");
    lv_obj_set_style_text_color(remote_button, lv_color_hex(0xA8D9FF), 0);
    lv_obj_align(remote_button, LV_ALIGN_BOTTOM_LEFT, 18, -18);
    lv_obj_add_flag(remote_button, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(remote_button, remote_button_event, LV_EVENT_CLICKED, NULL);

    qr_button = lv_label_create(screen);
    lv_label_set_text(qr_button, "QR");
    lv_obj_set_style_text_color(qr_button, lv_color_hex(0xA8D9FF), 0);
    lv_obj_align(qr_button, LV_ALIGN_BOTTOM_RIGHT, -18, -18);
    lv_obj_add_flag(qr_button, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(qr_button, qr_button_event, LV_EVENT_CLICKED, NULL);

    render_emotion(COMPANION_CALM);
    lv_timer_create(autonomous_timer, 8000, NULL);
    lv_timer_create(blink_timer, 2600, NULL);
    bsp_display_unlock();

    err = companion_ble_init();
    if (err != ESP_OK) ESP_LOGW(TAG, "BLE HID init failed: %s", esp_err_to_name(err));

    err = companion_wifi_start(setup_info, sizeof(setup_info));
    if (err == ESP_OK && setup_info[0]) {
        ESP_LOGI(TAG, "Secure Wi-Fi setup code: %s", setup_info);
        if (bsp_display_lock(pdMS_TO_TICKS(1000)) == ESP_OK) {
            char line[64];
            snprintf(line, sizeof(line), "%s", setup_info);
            lv_label_set_text(setup_label, line);
            bsp_display_unlock();
        }
    } else if (err != ESP_OK) {
        ESP_LOGW(TAG, "Wi-Fi startup failed: %s", esp_err_to_name(err));
    }

    if (companion_audio_init() == ESP_OK) {
        audio_available = true;
        companion_audio_set_temperament(state.temperament);
        xTaskCreate(voice_task, "companion_voice", 8192, NULL, 4, NULL);
    } else {
        ESP_LOGW(TAG, "Audio codec unavailable; touch and local emotions still work");
    }
    if (companion_imu_init() == ESP_OK) {
        imu_available = true;
        xTaskCreate(imu_task, "companion_imu", 4096, NULL, 3, NULL);
    } else {
        ESP_LOGW(TAG, "QMI8658 unavailable; IMU reactions disabled");
    }
    xTaskCreate(network_setup_task, "companion_setup", 4096, NULL, 2, NULL);
    xTaskCreate(power_task, "companion_power", 4096, NULL, 2, NULL);
    ESP_LOGI(TAG, "AI Companion firmware 2.3.0 started. Nonverbal output only; offline local mode retained.");
    if (companion_usb_start(on_usb_command) != ESP_OK) {
        ESP_LOGW(TAG, "USB Serial/JTAG control protocol unavailable");
    }
}
