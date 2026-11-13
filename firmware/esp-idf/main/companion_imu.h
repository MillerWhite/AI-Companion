#pragma once

#include <stdint.h>
#include "esp_err.h"

esp_err_t companion_imu_init(void);
uint32_t companion_imu_poll(void);

void companion_imu_get_tilt(float *x, float *y);
void companion_imu_get_orientation(float *pitch, float *roll, float *yaw);
