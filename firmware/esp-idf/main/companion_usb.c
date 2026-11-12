#include "companion_usb.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cJSON.h"
#include "driver/usb_serial_jtag.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include "companion_power.h"

#define USB_LINE_CAPACITY 192
#define USB_FIRMWARE_VERSION "2.3.0"

static const char *TAG = "companion_usb";
static companion_usb_command_handler_t command_handler;

static void usb_write_line(const char *line)
{
    if (!line) return;
    (void)usb_serial_jtag_write_bytes(line, (uint32_t)strlen(line), pdMS_TO_TICKS(100));
}

static esp_err_t nvs_read_string(const char *key, char *value, size_t capacity)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open("companion", NVS_READONLY, &handle);
    if (err != ESP_OK) return err;
    size_t size = capacity;
    err = nvs_get_str(handle, key, value, &size);
    nvs_close(handle);
    return err;
}

static uint8_t nvs_read_u8(const char *key, uint8_t fallback, uint8_t maximum)
{
    nvs_handle_t handle;
    uint8_t value = fallback;
    if (nvs_open("companion", NVS_READONLY, &handle) == ESP_OK) {
        if (nvs_get_u8(handle, key, &value) != ESP_OK || value > maximum) value = fallback;
        nvs_close(handle);
    }
    return value;
}

static void send_config(void)
{
    char name[48] = "Milo";
    (void)nvs_read_string("companion_name", name, sizeof(name));
    name[sizeof(name) - 1] = '\0';

    cJSON *json = cJSON_CreateObject();
    if (!json) return;
    cJSON_AddStringToObject(json, "version", USB_FIRMWARE_VERSION);
    cJSON_AddStringToObject(json, "name", name);
    cJSON_AddNumberToObject(json, "temperament", nvs_read_u8("temperament", 0, 4));
    cJSON_AddNumberToObject(json, "initiative", nvs_read_u8("initiative", 0, 2));
    cJSON_AddNumberToObject(json, "quirk", nvs_read_u8("quirk", 0, 4));
    cJSON_AddNumberToObject(json, "brightness", nvs_read_u8("brightness", 85, 100));
    cJSON_AddNumberToObject(json, "theme", nvs_read_u8("theme", 0, 4));
    cJSON_AddNumberToObject(json, "sleep_timeout", nvs_read_u8("sleep_timeout", 30, 300));
    char owner[48] = "", badge[64] = "", qr[4][180] = {{0}};
    (void)nvs_read_string("owner_name", owner, sizeof(owner));
    (void)nvs_read_string("owner_badge", badge, sizeof(badge));
    cJSON_AddStringToObject(json, "owner", owner);
    cJSON_AddStringToObject(json, "badge", badge);
    static const char *const qr_keys[4] = {"qr0", "qr1", "qr2", "qr3"};
    for (int i = 0; i < 4; ++i) {
        (void)nvs_read_string(qr_keys[i], qr[i], sizeof(qr[i]));
        cJSON_AddStringToObject(json, qr_keys[i], qr[i]);
    }
    char *payload = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);
    if (!payload) return;

    char line[1200];
    int written = snprintf(line, sizeof(line), "@C %s\n", payload);
    cJSON_free(payload);
    if (written > 0 && (size_t)written < sizeof(line)) usb_write_line(line);
}

static void send_status(void)
{
    cJSON *json = cJSON_CreateObject();
    if (!json) return;
    cJSON_AddBoolToObject(json, "usb", true);
    cJSON_AddBoolToObject(json, "vbus", companion_power_vbus_present());
    cJSON_AddBoolToObject(json, "charging", companion_power_is_charging());
    const int battery = companion_power_battery_percent();
    if (battery >= 0) cJSON_AddNumberToObject(json, "battery", battery);
    char *payload = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);
    if (!payload) return;
    char line[256];
    int written = snprintf(line, sizeof(line), "@S %s\n", payload);
    cJSON_free(payload);
    if (written > 0 && (size_t)written < sizeof(line)) usb_write_line(line);
}

static bool save_name(const char *name)
{
    if (!name || !*name) return false;
    size_t length = strlen(name);
    if (length > 40) return false;
    for (size_t i = 0; i < length; ++i) {
        unsigned char ch = (unsigned char)name[i];
        if (ch < 0x20 || ch == 0x7f) return false;
    }
    nvs_handle_t handle;
    if (nvs_open("companion", NVS_READWRITE, &handle) != ESP_OK) return false;
    esp_err_t err = nvs_set_str(handle, "companion_name", name);
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    return err == ESP_OK;
}

static bool save_profile_string(const char *key, const char *value, size_t maximum)
{
    if (!value) return false;
    if (!strcmp(value, "-")) value = "";
    size_t length = strlen(value);
    if (length > maximum) return false;
    for (size_t i = 0; i < length; ++i) {
        unsigned char ch = (unsigned char)value[i];
        if (ch < 0x20 || ch == 0x7f) return false;
    }
    nvs_handle_t handle;
    if (nvs_open("companion", NVS_READWRITE, &handle) != ESP_OK) return false;
    esp_err_t err = nvs_set_str(handle, key, value);
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    return err == ESP_OK;
}

static bool save_u8(const char *key, const char *value, uint8_t maximum)
{
    if (!value || !*value) return false;
    char *end = NULL;
    long parsed = strtol(value, &end, 10);
    if (!end || *end || parsed < 0 || parsed > maximum) return false;
    nvs_handle_t handle;
    if (nvs_open("companion", NVS_READWRITE, &handle) != ESP_OK) return false;
    esp_err_t err = nvs_set_u8(handle, key, (uint8_t)parsed);
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    return err == ESP_OK;
}

static void process_command(char *line)
{
    while (*line == ' ' || *line == '\t' || *line == '\r') ++line;
    if (!*line) return;
    if (!strcmp(line, "config")) { send_config(); return; }
    if (!strcmp(line, "status") || !strcmp(line, "ping")) { send_status(); return; }

    bool profile_changed = false;
    if (!strncmp(line, "name ", 5)) {
        if (!save_name(line + 5)) { usb_write_line("@E invalid_name\n"); return; }
        profile_changed = true;
    } else if (!strncmp(line, "temperament ", 12)) {
        if (!save_u8("temperament", line + 12, 4)) { usb_write_line("@E invalid_temperament\n"); return; }
        profile_changed = true;
    } else if (!strncmp(line, "initiative ", 11)) {
        if (!save_u8("initiative", line + 11, 2)) { usb_write_line("@E invalid_initiative\n"); return; }
        profile_changed = true;
    } else if (!strncmp(line, "quirk ", 6)) {
        if (!save_u8("quirk", line + 6, 4)) { usb_write_line("@E invalid_quirk\n"); return; }
        profile_changed = true;
        } else if (!strncmp(line, "brightness ", 11)) {
        if (!save_u8("brightness", line + 11, 100)) { usb_write_line("@E invalid_brightness\n"); return; }
        profile_changed = true;
    } else if (!strncmp(line, "theme ", 6)) {
        if (!save_u8("theme", line + 6, 4)) { usb_write_line("@E invalid_theme\n"); return; }
        profile_changed = true;
    } else if (!strncmp(line, "sleep-timeout ", 14)) {
        if (!save_u8("sleep_timeout", line + 14, 240)) { usb_write_line("@E invalid_sleep_timeout\n"); return; }
        profile_changed = true;
    } else if (!strncmp(line, "owner ", 6)) {
        if (!save_profile_string("owner_name", line + 6, 40)) { usb_write_line("@E invalid_owner\n"); return; }
        profile_changed = true;
    } else if (!strncmp(line, "badge ", 6)) {
        if (!save_profile_string("owner_badge", line + 6, 56)) { usb_write_line("@E invalid_badge\n"); return; }
        profile_changed = true;
    } else if (!strncmp(line, "qr", 2) && line[2] >= '0' && line[2] <= '3' && line[3] == ' ') {
        char key[4] = {'q', 'r', line[2], 0};
        if (!save_profile_string(key, line + 4, 170)) { usb_write_line("@E invalid_qr\n"); return; }
        profile_changed = true;
    }

    if (profile_changed) {
        if (command_handler) command_handler("profile");
        send_config();
        return;
    }

    if (!strcmp(line, "sleep") || !strcmp(line, "wake") ||
        !strcmp(line, "pet") || !strcmp(line, "play") ||
        !strcmp(line, "feed") || !strcmp(line, "call") || !strcmp(line, "clean") ||
        !strcmp(line, "wifi-reset") || !strncmp(line, "emotion ", 8) ||
        !strncmp(line, "sound ", 6)) {
        if (command_handler) command_handler(line);
        send_status();
        return;
    }
    usb_write_line("@E unknown_command\n");
}

static void usb_task(void *argument)
{
    (void)argument;
    usb_serial_jtag_driver_config_t config = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    config.rx_buffer_size = 512;
    esp_err_t err = usb_serial_jtag_driver_install(&config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "USB Serial/JTAG driver install failed: %s", esp_err_to_name(err));
        vTaskDelete(NULL);
        return;
    }

    static char line[USB_LINE_CAPACITY];
    size_t length = 0;
    uint8_t buffer[64];
    send_status();
    send_config();

    for (;;) {
        int received = usb_serial_jtag_read_bytes(buffer, sizeof(buffer), pdMS_TO_TICKS(50));
        for (int i = 0; i < received; ++i) {
            if (buffer[i] == '\n') {
                line[length] = '\0';
                process_command(line);
                length = 0;
            } else if (buffer[i] != '\r' && length + 1 < sizeof(line)) {
                line[length++] = (char)buffer[i];
            } else if (length + 1 >= sizeof(line)) {
                length = 0;
                usb_write_line("@E command_too_long\n");
            }
        }
    }
}

esp_err_t companion_usb_start(companion_usb_command_handler_t handler)
{
    command_handler = handler;
    if (xTaskCreate(usb_task, "companion_usb", 4096, NULL, 3, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
