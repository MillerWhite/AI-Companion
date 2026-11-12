#include "companion_power.h"

#include "driver/i2c_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "bsp/esp-bsp.h"

#define AXP2101_ADDRESS 0x34
#define AXP2101_STATUS1 0x00
#define AXP2101_STATUS2 0x01
#define AXP2101_BAT_PERCENT 0xA4

static const char *TAG = "companion_power";
static i2c_master_dev_handle_t pmu;
static bool ready;
static bool previous_vbus;
static bool previous_charging;
static bool charging;
static bool vbus_present;
static int battery_percent = -1;

static esp_err_t read_reg(uint8_t reg, uint8_t *value)
{
    if (!pmu || !value) return ESP_ERR_INVALID_STATE;
    return i2c_master_transmit_receive(pmu, &reg, 1, value, 1, pdMS_TO_TICKS(100));
}

esp_err_t companion_power_init(void)
{
    if (ready) return ESP_OK;
    i2c_master_bus_handle_t bus = bsp_i2c_get_handle();
    if (!bus) return ESP_ERR_INVALID_STATE;

    const i2c_device_config_t config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = AXP2101_ADDRESS,
        .scl_speed_hz = 100000,
    };
    esp_err_t err = i2c_master_bus_add_device(bus, &config, &pmu);
    if (err != ESP_OK) return err;

    uint8_t id = 0;
    /* AXP2101 register 0x03 contains the PMIC type (0x4A). */
    uint8_t type = 0;
    err = read_reg(0x03, &type);
    if (err != ESP_OK || type != 0x4A) {
        ESP_LOGW(TAG, "AXP2101 not detected (type=0x%02x)", type);
        i2c_master_bus_rm_device(pmu);
        pmu = NULL;
        return err == ESP_OK ? ESP_ERR_NOT_FOUND : err;
    }
    (void)id;
    ready = true;
    ESP_LOGI(TAG, "AXP2101 power telemetry ready");
    (void)companion_power_poll();
    return ESP_OK;
}

uint32_t companion_power_poll(void)
{
    if (!ready) return 0;
    uint8_t status1 = 0, status2 = 0;
    if (read_reg(AXP2101_STATUS1, &status1) != ESP_OK ||
        read_reg(AXP2101_STATUS2, &status2) != ESP_OK) return 0;

    /* STATUS1 bit 5 is VBUS-good. STATUS2 bit 3 is active-low VBUS-in.
       STATUS2 bits 7:5 encode charging/discharging/standby. */
    vbus_present = ((status2 & 0x08U) == 0U) && ((status1 & 0x20U) != 0U);
    charging = ((status2 >> 5U) & 0x07U) == 0x01U;

    uint8_t percent = 0xFF;
    if (read_reg(AXP2101_BAT_PERCENT, &percent) == ESP_OK && percent <= 100U) {
        battery_percent = (int)percent;
    }

    uint32_t events = 0;
    if (vbus_present && !previous_vbus) events |= COMPANION_POWER_CHARGER_CONNECTED;
    if (!vbus_present && previous_vbus) events |= COMPANION_POWER_CHARGER_REMOVED;
    if (charging && !previous_charging) events |= COMPANION_POWER_CHARGING;
    previous_vbus = vbus_present;
    previous_charging = charging;
    return events;
}

bool companion_power_is_charging(void) { return ready && charging; }
bool companion_power_vbus_present(void) { return ready && vbus_present; }
int companion_power_battery_percent(void) { return ready ? battery_percent : -1; }
