#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

enum {
    COMPANION_POWER_CHARGER_CONNECTED = 1U,
    COMPANION_POWER_CHARGING = 2U,
    COMPANION_POWER_CHARGER_REMOVED = 4U
};

esp_err_t companion_power_init(void);
uint32_t companion_power_poll(void);
bool companion_power_is_charging(void);
bool companion_power_vbus_present(void);
int companion_power_battery_percent(void);
