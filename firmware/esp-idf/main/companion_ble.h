#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

typedef enum {
    COMPANION_BLE_PLAY_PAUSE,
    COMPANION_BLE_NEXT,
    COMPANION_BLE_PREVIOUS,
    COMPANION_BLE_VOLUME_UP,
    COMPANION_BLE_VOLUME_DOWN,
    COMPANION_BLE_MUTE,
    COMPANION_BLE_PRESENTATION_NEXT,
    COMPANION_BLE_PRESENTATION_PREVIOUS,
    COMPANION_BLE_PRESENTATION_START,
    COMPANION_BLE_PRESENTATION_BLACK,
    COMPANION_BLE_ESCAPE
} companion_ble_key_t;

esp_err_t companion_ble_init(void);
bool companion_ble_hid_connected(void);
esp_err_t companion_ble_send_key(companion_ble_key_t key);
esp_err_t companion_ble_send_mouse(int8_t dx, int8_t dy, int8_t wheel, uint8_t buttons);
esp_err_t companion_ble_mouse_click(bool right_button);
esp_err_t companion_ble_send_keyboard(uint8_t modifiers, uint8_t keycode);
