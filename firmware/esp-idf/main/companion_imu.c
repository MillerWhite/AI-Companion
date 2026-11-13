#include "companion_imu.h"

#include "companion_motion.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_check.h"
#include "bsp/esp-bsp.h"
#include "qmi8658.h"

#define GRAVITY_MPS2 9.80665f

static const char *TAG = "companion_imu";
static qmi8658_dev_t sensor;
static companion_motion_state_t motion;
static companion_orientation_t orientation;
static bool ready;
static int64_t last_sample_us;
static float visual_tilt_x, visual_tilt_y;

esp_err_t companion_imu_init(void)
{
    if (ready) return ESP_OK;
    i2c_master_bus_handle_t bus = bsp_i2c_get_handle();
    if (!bus) return ESP_ERR_INVALID_STATE;
    ESP_RETURN_ON_ERROR(qmi8658_init(&sensor, bus, QMI8658_ADDRESS_HIGH), TAG, "QMI8658 init failed");
    ESP_RETURN_ON_ERROR(qmi8658_set_accel_range(&sensor, QMI8658_ACCEL_RANGE_8G), TAG, "accelerometer range");
    ESP_RETURN_ON_ERROR(qmi8658_set_accel_odr(&sensor, QMI8658_ACCEL_ODR_500HZ), TAG, "accelerometer ODR");
    ESP_RETURN_ON_ERROR(qmi8658_set_accel_unit_mps2(&sensor, true), TAG, "accelerometer units");
    ESP_RETURN_ON_ERROR(qmi8658_set_gyro_range(&sensor, QMI8658_GYRO_RANGE_512DPS), TAG, "gyroscope range");
    ESP_RETURN_ON_ERROR(qmi8658_set_gyro_odr(&sensor, QMI8658_GYRO_ODR_500HZ), TAG, "gyroscope ODR");
    ESP_RETURN_ON_ERROR(qmi8658_set_gyro_unit_rads(&sensor, true), TAG, "gyroscope units");
    motion = (companion_motion_state_t){0};
    orientation = (companion_orientation_t){0};
    last_sample_us = 0;
    ready = true;
    ESP_LOGI(TAG, "QMI8658 ready; Pip-derived shake, face-down, upside-down, fall and step classifier enabled");
    return ESP_OK;
}

uint32_t companion_imu_poll(void)
{
    if (!ready) return 0;
    bool data_ready = false;
    if (qmi8658_is_data_ready(&sensor, &data_ready) != ESP_OK || !data_ready) return 0;
    qmi8658_data_t data;
    if (qmi8658_read_sensor_data(&sensor, &data) != ESP_OK) return 0;

    const int64_t now = esp_timer_get_time();
    const float dt = last_sample_us ? (float)(now - last_sample_us) / 1000000.0f : 0.05f;
    last_sample_us = now;
    if (dt <= 0.0f || dt > 0.5f) return 0;

    /* QMI8658 is rotated 90 degrees on the Waveshare board.
       Map sensor axes to device axes: X = -sensor Y, Y = sensor X, Z = sensor Z. */
    /* Waveshare board axes: device X=-sensor Y, device Y=sensor X, Z=sensor Z.
       Apply the same rotation to gyro axes so all three rotations stay coherent. */
    const float ax = -data.accelY / GRAVITY_MPS2;
    const float ay = data.accelX / GRAVITY_MPS2;
    const float az = data.accelZ / GRAVITY_MPS2;
    const float gx = -data.gyroY;
    const float gy = data.gyroX;
    const float gz = data.gyroZ;
    companion_orientation_feed(&orientation, ax, ay, az, gx, gy, gz, dt);
    const uint32_t events = companion_motion_feed(&motion, ax, ay, az, dt);
    visual_tilt_x = motion.tilt_x;
    visual_tilt_y = motion.tilt_y;
    return events;
}

void companion_imu_get_tilt(float *x, float *y)
{
    if (x) *x = visual_tilt_x;
    if (y) *y = visual_tilt_y;
}

void companion_imu_get_orientation(float *pitch, float *roll, float *yaw)
{
    if (pitch) *pitch = orientation.pitch;
    if (roll) *roll = orientation.roll;
    if (yaw) *yaw = orientation.yaw;
}
