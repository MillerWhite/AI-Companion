#include "companion_ble.h"
#include <string.h>
#include "esp_log.h"
#include "esp_bt.h"
#include "esp_hidd.h"
#include "esp_hid_common.h"
#include "host/ble_hs.h"
#include "host/ble_gap.h"
#include "host/ble_hs_adv.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "store/config/ble_store_config.h"

static const char *TAG = "companion_ble";
static esp_hidd_dev_t *s_hid;
static volatile bool s_connected;
static bool s_started;

static const uint8_t s_media_report_map[] = {
    0x05,0x0C, 0x09,0x01, 0xA1,0x01, 0x85,0x01,
    0x15,0x00, 0x26,0xFF,0x03, 0x19,0x00, 0x2A,0xFF,0x03,
    0x75,0x10, 0x95,0x01, 0x81,0x00, 0xC0
};
static esp_hid_raw_report_map_t s_report_maps[] = {
    {.data = (uint8_t *)s_media_report_map, .len = sizeof(s_media_report_map)}
};
static esp_hid_device_config_t s_hid_config = {
    .vendor_id = 0xCafe, .product_id = 0x4010, .version = 0x0100,
    .device_name = "AI Companion Remote",
    .manufacturer_name = "AI Companion",
    .serial_number = "AICOMPANION01",
    .report_maps = s_report_maps, .report_maps_len = 1
};

static void adv_start(void)
{
    uint8_t own_addr_type;
    if (ble_hs_id_infer_auto(0, &own_addr_type) != 0) return;
    struct ble_hs_adv_fields fields;
    memset(&fields, 0, sizeof(fields));
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.name = (uint8_t *)s_hid_config.device_name;
    fields.name_len = strlen(s_hid_config.device_name);
    fields.name_is_complete = 1;
    fields.appearance = 0x03C4; /* Generic Remote Control */
    fields.appearance_is_present = 1;
    if (ble_gap_adv_set_fields(&fields) != 0) {
        ESP_LOGE(TAG, "BLE advertising fields rejected");
        return;
    }
    struct ble_gap_adv_params params;
    memset(&params, 0, sizeof(params));
    params.conn_mode = BLE_GAP_CONN_MODE_UND;
    params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    params.itvl_min = 0x30;
    params.itvl_max = 0x60;
    int rc = ble_gap_adv_start(own_addr_type, NULL, BLE_HS_FOREVER, &params, NULL, NULL);
    if (rc != 0) ESP_LOGE(TAG, "BLE advertising failed: %d", rc);
}
static void host_sync(void) { adv_start(); }
static void host_reset(int reason) { ESP_LOGW(TAG, "BLE host reset: %d", reason); s_connected = false; }
static int gap_event(struct ble_gap_event *event, void *arg)
{
    (void)arg;
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        s_connected = event->connect.status == 0;
        if (!s_connected) adv_start();
        ESP_LOGI(TAG, "HID connection %s", s_connected ? "established" : "failed");
        return 0;
    case BLE_GAP_EVENT_DISCONNECT:
        s_connected = false;
        ESP_LOGI(TAG, "HID disconnected; advertising again");
        adv_start();
        return 0;
    case BLE_GAP_EVENT_ADV_COMPLETE:
        adv_start();
        return 0;
    default: return 0;
    }
}
static void host_task(void *arg)
{
    (void)arg;
    nimble_port_run();
    nimble_port_freertos_deinit();
}
static void hid_event(void *handler_args, esp_event_base_t base, int32_t id, void *data)
{
    (void)handler_args; (void)base;
    esp_hidd_event_data_t *event = (esp_hidd_event_data_t *)data;
    if (id == ESP_HIDD_CONNECT_EVENT) s_connected = true;
    else if (id == ESP_HIDD_DISCONNECT_EVENT) s_connected = false;
    else if (id == ESP_HIDD_START_EVENT) ESP_LOGI(TAG, "HID GATT service started");
    (void)event;
}
esp_err_t companion_ble_init(void)
{
    if (s_started) return ESP_OK;
    ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT));
    esp_err_t err = nimble_port_init();
    if (err != ESP_OK) return err;
    ble_hs_cfg.sync_cb = host_sync;
    ble_hs_cfg.reset_cb = host_reset;
    ble_store_config_init();
    err = esp_hidd_dev_init(&s_hid_config, ESP_HID_TRANSPORT_BLE, hid_event, &s_hid);
    if (err != ESP_OK) return err;
    err = nimble_port_freertos_init(host_task);
    if (err != ESP_OK) return err;
    s_started = true;
    ESP_LOGI(TAG, "BLE media HID initialized");
    return ESP_OK;
}
bool companion_ble_hid_connected(void) { return s_connected; }
esp_err_t companion_ble_send_key(companion_ble_key_t key)
{
    if (!s_hid || !s_connected) return ESP_ERR_INVALID_STATE;
    uint16_t usage = 0;
    switch (key) {
    case COMPANION_BLE_PLAY_PAUSE: usage = 0x00CD; break;
    case COMPANION_BLE_NEXT: usage = 0x00B5; break;
    case COMPANION_BLE_PREVIOUS: usage = 0x00B6; break;
    case COMPANION_BLE_VOLUME_UP: usage = 0x00E9; break;
    case COMPANION_BLE_VOLUME_DOWN: usage = 0x00EA; break;
    case COMPANION_BLE_MUTE: usage = 0x00E2; break;
    default: return ESP_ERR_INVALID_ARG;
    }
    uint8_t report[2] = {(uint8_t)(usage & 0xFF), (uint8_t)(usage >> 8)};
    esp_err_t err = esp_hidd_dev_input_set(s_hid, 0, 1, report, sizeof(report));
    if (err != ESP_OK) return err;
    vTaskDelay(pdMS_TO_TICKS(35));
    report[0] = report[1] = 0;
    return esp_hidd_dev_input_set(s_hid, 0, 1, report, sizeof(report));
}
