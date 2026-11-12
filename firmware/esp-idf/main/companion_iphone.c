/*
 * AI Companion iPhone accessory client.
 * Implements Apple Notification Center Service (ANCS) and Apple Media Service (AMS)
 * as GATT client procedures on the same BLE link used by the HID accessory.
 */
#include "companion_iphone.h"
#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "host/ble_hs.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/ble_uuid.h"
#include "host/ble_store.h"
#include "host/ble_hs_adv.h"
#include "services/gap/ble_svc_gap.h"

static const char *TAG = "companion_iphone";
static uint16_t s_conn = BLE_HS_CONN_HANDLE_NONE;
static uint16_t s_ancs_end, s_ancs_ns, s_ancs_cp, s_ancs_ds;
static uint16_t s_ams_end, s_ams_remote, s_ams_entity_update, s_ams_entity_attr;
static bool s_ancs_ready, s_ams_ready, s_ams_started;
static uint8_t s_ancs_desc_pending;
static uint8_t s_pending_entity, s_pending_attribute;
static struct { uint8_t entity, attribute; } s_attr_queue[8];
static uint8_t s_attr_head, s_attr_tail, s_attr_count;
static bool s_attr_in_flight;
static void ams_process_next_attribute_request(void);
static uint8_t s_data[512];
static size_t s_data_len;
static uint8_t s_data_attr_mask;
static char s_notification[240], s_track_title[96], s_track_artist[96], s_player[48];
static uint8_t s_playback_state;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static struct ble_gap_event_listener s_listener;
static void start_ams_discovery(uint16_t conn);
static bool s_listener_registered;

static const ble_uuid128_t UUID_ANCS = BLE_UUID128_INIT(
    0xD0,0x00,0x2D,0x12,0x1E,0x4B,0x0F,0xA4,0x99,0x4E,0xCE,0xB5,0x31,0xF4,0x05,0x79);
static const ble_uuid128_t UUID_ANCS_NS = BLE_UUID128_INIT(
    0xBD,0x1D,0xA2,0x99,0xE6,0x25,0x58,0x8C,0xD9,0x42,0x01,0x63,0x0D,0x12,0xBF,0x9F);
static const ble_uuid128_t UUID_ANCS_CP = BLE_UUID128_INIT(
    0xD9,0xD9,0xAA,0xFD,0xBD,0x9B,0x21,0x98,0xA8,0x49,0xE1,0x45,0xF3,0xD8,0xD1,0x69);
static const ble_uuid128_t UUID_ANCS_DS = BLE_UUID128_INIT(
    0xFB,0x7B,0x7C,0xCE,0x6A,0xB3,0x44,0xBE,0xB5,0x4B,0xD6,0x24,0xE9,0xC6,0xEA,0x22);
static const ble_uuid128_t UUID_AMS = BLE_UUID128_INIT(
    0xDC,0xF8,0x55,0xAD,0x02,0xC5,0xF4,0x8E,0x3A,0x43,0x36,0x0F,0x2B,0x50,0xD3,0x89);
static const ble_uuid128_t UUID_AMS_REMOTE = BLE_UUID128_INIT(
    0xC2,0x51,0xCA,0xF7,0x56,0x0E,0xDF,0xB8,0x8A,0x4A,0xB1,0x57,0xD8,0x81,0x3C,0x9B);
static const ble_uuid128_t UUID_AMS_UPDATE = BLE_UUID128_INIT(
    0x02,0xC1,0x96,0xBA,0x92,0xBB,0x0C,0x9A,0x1F,0x41,0x8D,0x80,0xCE,0xAB,0x7C,0x2F);
static const ble_uuid128_t UUID_AMS_ATTRIBUTE = BLE_UUID128_INIT(
    0xD7,0xD5,0xBB,0x70,0xA8,0xA3,0xAB,0xA6,0xD8,0x46,0xAB,0x23,0x8C,0xF3,0xB2,0xC6);

static void reset_handles(void)
{
    s_ancs_end = s_ancs_ns = s_ancs_cp = s_ancs_ds = 0;
    s_ams_end = s_ams_remote = s_ams_entity_update = s_ams_entity_attr = 0;
    s_ancs_ready = s_ams_ready = s_ams_started = false;
    s_ancs_desc_pending = 0;
    s_data_len = 0;
    s_data_attr_mask = 0;
}

static int ams_track_subscribe_cb(uint16_t conn, const struct ble_gatt_error *error,
                                  struct ble_gatt_attr *attr, void *arg)
{
    (void)attr; (void)arg;
    if (error->status) { ESP_LOGW(TAG, "AMS track subscription failed: %d", error->status); return 0; }
    const uint8_t req[] = {0, 0, 1, 2};
    int rc = ble_gattc_write_flat(conn, s_ams_entity_update, req, sizeof(req), NULL, NULL);
    if (rc) ESP_LOGW(TAG, "AMS player subscription write failed: %d", rc);
    return 0;
}

static int cccd_write_cb(uint16_t conn, const struct ble_gatt_error *error,
                         struct ble_gatt_attr *attr, void *arg)
{
    (void)attr;
    uint16_t value_handle = (uint16_t)(uintptr_t)arg;
    if (error->status) {
        ESP_LOGW(TAG, "CCCD subscribe failed handle=%u status=%d", value_handle, error->status);
        return 0;
    }
    ESP_LOGI(TAG, "Notifications enabled handle=%u", value_handle);
    if (value_handle == s_ams_entity_update && s_ams_entity_update) {
        /* EntityUpdate: subscribe to track title/artist/album/duration and player state/name/volume. */
        const uint8_t req[] = {2, 0, 1, 2, 3};
        int rc = ble_gattc_write_flat(conn, s_ams_entity_update, req, sizeof(req), ams_track_subscribe_cb, NULL);
        if (rc) ESP_LOGW(TAG, "AMS track subscription write failed: %d", rc);
    }
    return 0;
}

/* A descriptor discovery callback is shared by ANCS and AMS notification characteristics. */
static int dsc_cb(uint16_t conn, const struct ble_gatt_error *error,
                  uint16_t chr_val_handle, const struct ble_gatt_dsc *dsc, void *arg)
{
    (void)arg;
    if (error->status == BLE_HS_EDONE) {
        if ((uintptr_t)arg == 1 && s_ancs_desc_pending) {
            s_ancs_desc_pending--;
            if (s_ancs_desc_pending == 0) start_ams_discovery(conn);
        }
        return 0;
    }
    if (error->status) return error->status;
    if (ble_uuid_cmp(&dsc->uuid.u, BLE_UUID16_DECLARE(BLE_GATT_DSC_CLT_CFG_UUID16)) == 0) {
        const uint8_t enable[2] = {1,0};
        int rc = ble_gattc_write_flat(conn, dsc->handle, enable, sizeof(enable),
                                      cccd_write_cb, (void *)(uintptr_t)chr_val_handle);
        if (rc) ESP_LOGW(TAG, "CCCD write queue failed: %d", rc);
    }
    return 0;
}

static int discover_ancs_chr(uint16_t conn, const struct ble_gatt_error *error,
                             const struct ble_gatt_chr *chr, void *arg)
{
    (void)arg;
    if (error->status == BLE_HS_EDONE) {
        s_ancs_ready = s_ancs_ns && s_ancs_cp && s_ancs_ds;
        s_ancs_desc_pending = (s_ancs_ns ? 1 : 0) + (s_ancs_ds ? 1 : 0);
        if (s_ancs_ns && ble_gattc_disc_all_dscs(conn, s_ancs_ns, s_ancs_end, dsc_cb, (void *)1) != 0) s_ancs_desc_pending--;
        if (s_ancs_ds && ble_gattc_disc_all_dscs(conn, s_ancs_ds, s_ancs_end, dsc_cb, (void *)1) != 0) s_ancs_desc_pending--;
        if (!s_ancs_desc_pending) start_ams_discovery(conn);
        ESP_LOGI(TAG, "ANCS discovery %s", s_ancs_ready ? "complete" : "incomplete");
        return 0;
    }
    if (error->status) return error->status;
    if (ble_uuid_cmp(&chr->uuid.u, &UUID_ANCS_NS.u) == 0) s_ancs_ns = chr->val_handle;
    else if (ble_uuid_cmp(&chr->uuid.u, &UUID_ANCS_CP.u) == 0) s_ancs_cp = chr->val_handle;
    else if (ble_uuid_cmp(&chr->uuid.u, &UUID_ANCS_DS.u) == 0) s_ancs_ds = chr->val_handle;
    return 0;
}

static int discover_ancs(uint16_t conn, const struct ble_gatt_error *error,
                         const struct ble_gatt_svc *svc, void *arg)
{
    (void)arg;
    if (error->status == BLE_HS_EDONE) {
        if (!s_ancs_end) {
            ESP_LOGI(TAG, "iPhone ANCS service not exposed on this connection");
            start_ams_discovery(conn);
        }
        return 0;
    }
    if (error->status) return error->status;
    if (!svc) return BLE_HS_EUNKNOWN;
    s_ancs_end = svc->end_handle;
    return ble_gattc_disc_all_chrs(conn, svc->start_handle, svc->end_handle, discover_ancs_chr, NULL);
}

static int discover_ams_chr(uint16_t conn, const struct ble_gatt_error *error,
                            const struct ble_gatt_chr *chr, void *arg)
{
    (void)arg;
    if (error->status == BLE_HS_EDONE) {
        if (s_ams_remote) (void)ble_gattc_disc_all_dscs(conn, s_ams_remote, s_ams_end, dsc_cb, NULL);
        if (s_ams_entity_update) (void)ble_gattc_disc_all_dscs(conn, s_ams_entity_update, s_ams_end, dsc_cb, NULL);
        s_ams_ready = s_ams_remote && s_ams_entity_update && s_ams_entity_attr;
        ESP_LOGI(TAG, "AMS discovery %s", s_ams_ready ? "complete" : "incomplete");
        return 0;
    }
    if (error->status) return error->status;
    if (ble_uuid_cmp(&chr->uuid.u, &UUID_AMS_REMOTE.u) == 0) s_ams_remote = chr->val_handle;
    else if (ble_uuid_cmp(&chr->uuid.u, &UUID_AMS_UPDATE.u) == 0) s_ams_entity_update = chr->val_handle;
    else if (ble_uuid_cmp(&chr->uuid.u, &UUID_AMS_ATTRIBUTE.u) == 0) s_ams_entity_attr = chr->val_handle;
    return 0;
}

static int discover_ams(uint16_t conn, const struct ble_gatt_error *error,
                        const struct ble_gatt_svc *svc, void *arg)
{
    (void)arg;
    if (error->status == BLE_HS_EDONE) {
        if (!s_ams_end) {
            ESP_LOGI(TAG, "iPhone AMS service not exposed yet; will retry while connected");
            s_ams_started = false;
        }
        return 0;
    }
    if (error->status) { s_ams_started = false; return error->status; }
    if (!svc) return BLE_HS_EUNKNOWN;
    s_ams_end = svc->end_handle;
    return ble_gattc_disc_all_chrs(conn, svc->start_handle, svc->end_handle, discover_ams_chr, NULL);
}

static void start_ams_discovery(uint16_t conn)
{
    if (s_ams_started) return;
    s_ams_started = true;
    int rc = ble_gattc_disc_svc_by_uuid(conn, &UUID_AMS.u, discover_ams, NULL);
    if (rc) {
        s_ams_started = false;
        ESP_LOGW(TAG, "AMS service discovery could not start: %d", rc);
    }
}

static void ancs_request_attributes(uint16_t conn, uint32_t uid)
{
    if (!s_ancs_cp) return;
    s_data_len = 0;
    s_data_attr_mask = 0;
    uint8_t req[] = {
        0, (uint8_t)uid, (uint8_t)(uid >> 8), (uint8_t)(uid >> 16), (uint8_t)(uid >> 24),
        0, 1, 0, 64, 0, 3, 0, 160, 0
    };
    int rc = ble_gattc_write_flat(conn, s_ancs_cp, req, sizeof(req), NULL, NULL);
    if (rc) ESP_LOGW(TAG, "ANCS attribute request failed: %d", rc);
}

static void parse_ancs_data(void)
{
    if (s_data_len < 5 || s_data[0] != 0) return;
    size_t pos = 5;
    char app[48] = "", title[80] = "", message[120] = "";
    bool parsed = false;
    while (pos + 3 <= s_data_len) {
        uint8_t id = s_data[pos++];
        uint16_t len = (uint16_t)s_data[pos] | ((uint16_t)s_data[pos+1] << 8);
        pos += 2;
        if (pos + len > s_data_len) break;
        size_t n = len;
        char *dst = NULL; size_t cap = 0;
        if (id == 0) { dst = app; cap = sizeof(app); s_data_attr_mask |= 1U; }
        else if (id == 1) { dst = title; cap = sizeof(title); s_data_attr_mask |= 2U; }
        else if (id == 3) { dst = message; cap = sizeof(message); s_data_attr_mask |= 8U; }
        if (dst && cap) { if (n >= cap) n = cap - 1; memcpy(dst, &s_data[pos], n); dst[n] = 0; }
        pos += len;
        parsed = true;
    }
    if (parsed && (title[0] || message[0])) {
        char out[240];
        if (app[0]) snprintf(out, sizeof(out), "%s: %s%s%s", app, title, title[0] && message[0] ? " — " : "", message);
        else snprintf(out, sizeof(out), "%s%s%s", title, title[0] && message[0] ? " — " : "", message);
        portENTER_CRITICAL(&s_lock);
        snprintf(s_notification, sizeof(s_notification), "%s", out);
        portEXIT_CRITICAL(&s_lock);
        ESP_LOGI(TAG, "iPhone notification: %s", out);
    }
    if (pos == s_data_len && (s_data_attr_mask & 0x0B) == 0x0B) { s_data_len = 0; s_data_attr_mask = 0; }
}

static void parse_ams_update(const uint8_t *data, size_t len);

static int ams_entity_attribute_read_cb(uint16_t conn, const struct ble_gatt_error *error,
                                        struct ble_gatt_attr *attr, void *arg)
{
    (void)conn; (void)arg;
    if (error->status == 0 && attr && attr->om) {
        uint8_t packet[131];
        size_t n = attr->om->om_len;
        if (n > sizeof(packet) - 3) n = sizeof(packet) - 3;
        packet[0] = s_pending_entity;
        packet[1] = s_pending_attribute;
        packet[2] = 0;
        memcpy(packet + 3, attr->om->om_data, n);
        parse_ams_update(packet, n + 3);
    } else if (error->status) {
        ESP_LOGW(TAG, "AMS full attribute read failed: %d", error->status);
    }
    s_attr_in_flight = false;
    ams_process_next_attribute_request();
    return 0;
}

static int ams_entity_attribute_write_cb(uint16_t conn, const struct ble_gatt_error *error,
                                         struct ble_gatt_attr *attr, void *arg)
{
    (void)attr; (void)arg;
    if (error->status) {
        ESP_LOGW(TAG, "AMS full attribute request failed: %d", error->status);
        s_attr_in_flight = false;
        ams_process_next_attribute_request();
        return 0;
    }
    int rc = ble_gattc_read(conn, s_ams_entity_attr, ams_entity_attribute_read_cb, NULL);
    if (rc) {
        ESP_LOGW(TAG, "AMS full attribute read could not start: %d", rc);
        s_attr_in_flight = false;
        ams_process_next_attribute_request();
    }
    return 0;
}

static void ams_process_next_attribute_request(void)
{
    if (s_attr_in_flight || !s_ams_entity_attr || !s_attr_count || s_conn == BLE_HS_CONN_HANDLE_NONE) return;
    s_pending_entity = s_attr_queue[s_attr_head].entity;
    s_pending_attribute = s_attr_queue[s_attr_head].attribute;
    s_attr_head = (uint8_t)((s_attr_head + 1U) % 8U);
    s_attr_count--;
    s_attr_in_flight = true;
    const uint8_t request[2] = {s_pending_entity, s_pending_attribute};
    int rc = ble_gattc_write_flat(s_conn, s_ams_entity_attr, request, sizeof(request),
                                  ams_entity_attribute_write_cb, NULL);
    if (rc) {
        ESP_LOGW(TAG, "AMS full attribute request could not be queued: %d", rc);
        s_attr_in_flight = false;
        ams_process_next_attribute_request();
    }
}

static void ams_queue_attribute_request(uint8_t entity, uint8_t attribute)
{
    if (s_attr_count >= 8) {
        ESP_LOGW(TAG, "AMS full attribute request queue full; dropping entity=%u attribute=%u", entity, attribute);
        return;
    }
    s_attr_queue[s_attr_tail].entity = entity;
    s_attr_queue[s_attr_tail].attribute = attribute;
    s_attr_tail = (uint8_t)((s_attr_tail + 1U) % 8U);
    s_attr_count++;
    ams_process_next_attribute_request();
}

static void parse_ams_update(const uint8_t *data, size_t len)
{
    if (!data || len < 3) return;
    uint8_t entity = data[0], attr = data[1], flags = data[2];
    const uint8_t *v = data + 3; size_t n = len - 3;
    if (flags & 1) ams_queue_attribute_request(entity, attr);
    portENTER_CRITICAL(&s_lock);
    if (entity == 2 && attr == 2) { size_t k = n < sizeof(s_track_title)-1 ? n : sizeof(s_track_title)-1; memcpy(s_track_title,v,k); s_track_title[k]=0; }
    else if (entity == 2 && attr == 0) { size_t k = n < sizeof(s_track_artist)-1 ? n : sizeof(s_track_artist)-1; memcpy(s_track_artist,v,k); s_track_artist[k]=0; }
    else if (entity == 0 && attr == 0) { size_t k = n < sizeof(s_player)-1 ? n : sizeof(s_player)-1; memcpy(s_player,v,k); s_player[k]=0; }
    else if (entity == 0 && attr == 1 && n) s_playback_state = (uint8_t)(v[0] - '0');
    portEXIT_CRITICAL(&s_lock);
    if (entity == 2 && (attr == 0 || attr == 2)) ESP_LOGI(TAG, "iPhone now playing: %s — %s", s_track_artist, s_track_title);
}

static int iphone_gap_event(struct ble_gap_event *event, void *arg)
{
    (void)arg;
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            s_conn = event->connect.conn_handle;
            reset_handles();
            ESP_LOGI(TAG, "iPhone/accessory BLE link connected; starting secure pairing");
            int rc = ble_gap_security_initiate(s_conn);
            if (rc) ESP_LOGW(TAG, "Pairing start returned %d (may already be bonded)", rc);
        }
        return 0;
    case BLE_GAP_EVENT_ENC_CHANGE:
        if (event->enc_change.conn_handle == s_conn && event->enc_change.status == 0) {
            ESP_LOGI(TAG, "Encrypted link ready; discovering ANCS and AMS");
            (void)ble_gattc_disc_svc_by_uuid(s_conn, &UUID_ANCS.u, discover_ancs, NULL);
        }
        return 0;
    case BLE_GAP_EVENT_NOTIFY_RX:
        if (event->notify_rx.conn_handle != s_conn || !event->notify_rx.om) return 0;
        if (event->notify_rx.attr_handle == s_ancs_ns && event->notify_rx.om->om_len >= 8) {
            const uint8_t *p = event->notify_rx.om->om_data;
            if (p[0] == 0 || p[0] == 1) {
                uint32_t uid = (uint32_t)p[4] | ((uint32_t)p[5]<<8) | ((uint32_t)p[6]<<16) | ((uint32_t)p[7]<<24);
                ancs_request_attributes(s_conn, uid);
            } else if (p[0] == 2) {
                portENTER_CRITICAL(&s_lock); s_notification[0] = 0; portEXIT_CRITICAL(&s_lock);
            }
        } else if (event->notify_rx.attr_handle == s_ancs_ds) {
            size_t n = event->notify_rx.om->om_len;
            if (n + s_data_len <= sizeof(s_data)) { memcpy(s_data+s_data_len,event->notify_rx.om->om_data,n); s_data_len += n; parse_ancs_data(); }
            else s_data_len = 0;
        } else if (event->notify_rx.attr_handle == s_ams_entity_update) {
            parse_ams_update(event->notify_rx.om->om_data, event->notify_rx.om->om_len);
        }
        return 0;
    case BLE_GAP_EVENT_DISCONNECT:
        if (event->disconnect.conn.conn_handle == s_conn) {
            s_conn = BLE_HS_CONN_HANDLE_NONE;
            reset_handles();
            ESP_LOGI(TAG, "iPhone BLE link disconnected");
        }
        return 0;
    case BLE_GAP_EVENT_REPEAT_PAIRING: {
        struct ble_gap_conn_desc desc;
        if (ble_gap_conn_find(event->repeat_pairing.conn_handle, &desc) == 0) {
            ble_store_util_delete_peer(&desc.peer_id_addr);
            return BLE_GAP_REPEAT_PAIRING_RETRY;
        }
        return BLE_GAP_REPEAT_PAIRING_IGNORE;
    }
    default: return 0;
    }
}

void companion_iphone_init(void)
{
    if (!s_listener_registered) {
        int rc = ble_gap_event_listener_register(&s_listener, iphone_gap_event, NULL);
        if (rc == 0) s_listener_registered = true;
        else ESP_LOGE(TAG, "Could not register iPhone GATT client listener: %d", rc);
    }
}

void companion_iphone_poll(void)
{
    /* iOS may publish AMS only after a media app starts. Retry discovery until it appears. */
    if (s_conn != BLE_HS_CONN_HANDLE_NONE && !s_ams_ready && !s_ams_started) {
        start_ams_discovery(s_conn);
    }
}

bool companion_iphone_connected(void) { return s_conn != BLE_HS_CONN_HANDLE_NONE && (s_ancs_ready || s_ams_ready); }
bool companion_iphone_ancs_ready(void) { return s_ancs_ready; }
bool companion_iphone_ams_ready(void) { return s_ams_ready; }

esp_err_t companion_iphone_media_command(companion_iphone_command_t command)
{
    if (!companion_iphone_connected() || !s_ams_ready || !s_ams_remote) return ESP_ERR_INVALID_STATE;
    uint8_t value = (uint8_t)command;
    int rc = ble_gattc_write_flat(s_conn, s_ams_remote, &value, sizeof(value), NULL, NULL);
    return rc == 0 ? ESP_OK : ESP_FAIL;
}

void companion_iphone_get_notification(char *out, size_t out_len)
{
    if (!out || !out_len) return;
    portENTER_CRITICAL(&s_lock); snprintf(out, out_len, "%s", s_notification); portEXIT_CRITICAL(&s_lock);
}
void companion_iphone_get_media(char *title, size_t title_len, char *artist, size_t artist_len,
                                char *player, size_t player_len, uint8_t *playback_state)
{
    portENTER_CRITICAL(&s_lock);
    if (title && title_len) snprintf(title, title_len, "%s", s_track_title);
    if (artist && artist_len) snprintf(artist, artist_len, "%s", s_track_artist);
    if (player && player_len) snprintf(player, player_len, "%s", s_player);
    if (playback_state) *playback_state = s_playback_state;
    portEXIT_CRITICAL(&s_lock);
}
