#include "companion_imu.h"

#include <math.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_check.h"
#include "bsp/esp-bsp.h"
#include "qmi8658.h"

static const char *TAG = "companion_imu";
static qmi8658_dev_t *imu;
static float filtered_x, filtered_y, filtered_z;
static bool ready;
static bool filter_initialized;
static int64_t last_motion_event_us;
static int64_t last_tilt_event_us;

esp_err_t companion_imu_init(void)
{
    if (ready) return ESP_OK;
    i2c_master_bus_handle_t bus = bsp_i2c_get_handle();
    if (!bus) return ESP_ERR_INVALID_STATE;
    imu = calloc(1, sizeof(*imu));
    if (!imu) return ESP_ERR_NO_MEM;
    ESP_RETURN_ON_ERROR(qmi8658_init(imu, bus, QMI8658_ADDRESS_HIGH), TAG, "QMI8658 init failed");
    ESP_RETURN_ON_ERROR(qmi8658_set_accel_range(imu, QMI8658_ACCEL_RANGE_8G), TAG, "accelerometer range");
    ESP_RETURN_ON_ERROR(qmi8658_set_accel_odr(imu, QMI8658_ACCEL_ODR_500HZ), TAG, "accelerometer ODR");
    ESP_RETURN_ON_ERROR(qmi8658_set_accel_unit_mps2(imu, true), TAG, "accelerometer units");
    ready = true;
    ESP_LOGI(TAG, "QMI8658 ready; filtered motion gestures enabled");
    return ESP_OK;
}

static bool read_motion(float *x, float *y, float *z)
{
    if (!ready || !imu) return false;
    qmi8658_data_t data;
    bool data_ready = false;
    if (qmi8658_is_data_ready(imu, &data_ready) != ESP_OK || !data_ready) return false;
    if (qmi8658_read_sensor_data(imu, &data) != ESP_OK) return false;
    if (!filter_initialized) {
        filtered_x = data.accelX; filtered_y = data.accelY; filtered_z = data.accelZ;
        filter_initialized = true;
    } else {
        filtered_x = filtered_x * 0.78f + data.accelX * 0.22f;
        filtered_y = filtered_y * 0.78f + data.accelY * 0.22f;
        filtered_z = filtered_z * 0.78f + data.accelZ * 0.22f;
    }
    *x = filtered_x; *y = filtered_y; *z = filtered_z;
    return true;
}

bool companion_imu_motion_event(void)
{
    float x, y, z;
    if (!read_motion(&x, &y, &z)) return false;
    const float magnitude = sqrtf(x*x + y*y + z*z);
    const int64_t now = esp_timer_get_time();
    if (fabsf(magnitude - 9.81f) > 5.2f && now - last_motion_event_us > 1800000LL) {
        last_motion_event_us = now;
        return true;
    }
    return false;
}

bool companion_imu_tilt_event(void)
{
    if (!filter_initialized) return false;
    const int64_t now = esp_timer_get_time();
    if ((fabsf(filtered_x) > 6.0f || fabsf(filtered_y) > 6.0f) && now - last_tilt_event_us > 3500000LL) {
        last_tilt_event_us = now;
        return true;
    }
    return false;
}
