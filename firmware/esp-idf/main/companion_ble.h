#pragma once
#include <stdbool.h>
#include "esp_err.h"
typedef enum { COMPANION_BLE_PLAY_PAUSE, COMPANION_BLE_NEXT, COMPANION_BLE_PREVIOUS, COMPANION_BLE_VOLUME_UP, COMPANION_BLE_VOLUME_DOWN, COMPANION_BLE_MUTE } companion_ble_key_t;
esp_err_t companion_ble_init(void);
esp_err_t companion_ble_send_key(companion_ble_key_t key);
bool companion_ble_hid_connected(void);
