#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_random.h"
#include "nvs_flash.h"
#include "bsp/esp-bsp.h"
#include "lvgl.h"

static const char *TAG = "ai_companion";
static lv_obj_t *eyes[2];
static lv_obj_t *mouth;
static lv_obj_t *status_label;

typedef enum {
    MOOD_CALM = 0,
    MOOD_HAPPY,
    MOOD_CURIOUS,
    MOOD_SLEEPY,
    MOOD_SAD,
    MOOD_COUNT
} companion_mood_t;

static const char *mood_text[MOOD_COUNT] = {
    "спокойно наблюдает",
    "рад тебя видеть",
    "заинтересован",
    "немного сонный",
    "грустит и ждёт внимания"
};

static void style_eye(lv_obj_t *eye)
{
    lv_obj_remove_style_all(eye);
    lv_obj_set_style_bg_color(eye, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(eye, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(eye, 0, 0);
    lv_obj_set_style_radius(eye, LV_RADIUS_CIRCLE, 0);
}

static void render_mood(companion_mood_t mood)
{
    int eye_w = 58, eye_h = 78, mouth_w = 28, mouth_h = 12;
    switch (mood) {
        case MOOD_HAPPY: eye_h = 54; mouth_w = 38; mouth_h = 18; break;
        case MOOD_CURIOUS: eye_w = 64; eye_h = 88; mouth_w = 15; mouth_h = 18; break;
        case MOOD_SLEEPY: eye_h = 7; mouth_w = 20; mouth_h = 4; break;
        case MOOD_SAD: eye_w = 54; eye_h = 56; mouth_w = 36; mouth_h = 15; break;
        default: break;
    }

    for (int i = 0; i < 2; ++i) {
        lv_obj_set_size(eyes[i], eye_w, eye_h);
        lv_obj_set_style_bg_color(eyes[i],
            mood == MOOD_SAD ? lv_color_hex(0xD9EEFF) : lv_color_hex(0xFFFFFF), 0);
    }

    lv_obj_set_size(mouth, mouth_w, mouth_h);
    lv_obj_set_style_border_width(mouth, 0, 0);
    lv_obj_set_style_border_side(mouth,
        mood == MOOD_SAD ? LV_BORDER_SIDE_TOP : LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(mouth, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_width(mouth, mood == MOOD_CURIOUS ? 2 : 4, 0);
    lv_obj_set_style_radius(mouth, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(mouth, mood == MOOD_CURIOUS ? LV_OPA_10 : LV_OPA_TRANSP, 0);
    lv_label_set_text(status_label, mood_text[mood]);
    ESP_LOGI(TAG, "Mood changed: %s", mood_text[mood]);
}

static void on_face_tap(lv_event_t *event)
{
    (void)event;
    render_mood(MOOD_HAPPY);
}

static void autonomous_behavior(lv_timer_t *timer)
{
    (void)timer;
    const uint32_t roll = esp_random() % 100;
    if (roll < 46) render_mood(MOOD_CALM);
    else if (roll < 67) render_mood(MOOD_CURIOUS);
    else if (roll < 83) render_mood(MOOD_HAPPY);
    else if (roll < 94) render_mood(MOOD_SLEEPY);
    else render_mood(MOOD_SAD);
}

void app_main(void)
{
    esp_err_t nvs_err = nvs_flash_init();
    if (nvs_err == ESP_ERR_NVS_NO_FREE_PAGES || nvs_err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        nvs_err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(nvs_err);

    /* Official Waveshare BSP starts the CO5300 AMOLED and LVGL touch path. */
    bsp_display_start();
    bsp_display_lock(-1);

    lv_obj_t *screen = lv_screen_active();
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

    lv_obj_t *face = lv_obj_create(screen);
    lv_obj_remove_style_all(face);
    lv_obj_set_size(face, 300, 220);
    lv_obj_align(face, LV_ALIGN_CENTER, 0, -5);
    lv_obj_add_flag(face, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(face, on_face_tap, LV_EVENT_CLICKED, NULL);

    for (int i = 0; i < 2; ++i) {
        eyes[i] = lv_obj_create(face);
        style_eye(eyes[i]);
        lv_obj_set_pos(eyes[i], i == 0 ? 70 : 174, 35);
        lv_obj_set_size(eyes[i], 58, 78);
        lv_obj_add_event_cb(eyes[i], on_face_tap, LV_EVENT_CLICKED, NULL);
    }

    mouth = lv_obj_create(face);
    lv_obj_remove_style_all(mouth);
    lv_obj_set_pos(mouth, 136, 145);
    lv_obj_set_size(mouth, 28, 12);
    lv_obj_add_flag(mouth, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(mouth, on_face_tap, LV_EVENT_CLICKED, NULL);

    status_label = lv_label_create(screen);
    lv_obj_set_style_text_color(status_label, lv_color_hex(0xB8B8C0), 0);
    lv_obj_align(status_label, LV_ALIGN_CENTER, 0, 150);

    lv_obj_t *footer = lv_label_create(screen);
    lv_label_set_text(footer, "AI COMPANION  ·  автономный режим");
    lv_obj_set_style_text_color(footer, lv_color_hex(0x55555D), 0);
    lv_obj_align(footer, LV_ALIGN_BOTTOM_MID, 0, -16);

    render_mood(MOOD_CALM);
    lv_timer_create(autonomous_behavior, 12000, NULL);
    bsp_display_unlock();
    ESP_LOGI(TAG, "Firmware started; local behavior does not require cloud.");
}
