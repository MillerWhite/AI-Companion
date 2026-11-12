#pragma once
#include <stdbool.h>
#include "esp_err.h"

esp_err_t companion_imu_init(void);
bool companion_imu_motion_event(void);
bool companion_imu_tilt_event(void);
