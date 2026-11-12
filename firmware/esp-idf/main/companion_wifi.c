#include "companion_wifi.h"

#include <stdio.h>
#include <string.h>
#include "esp_event.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "wifi_provisioning/manager.h"
#include "wifi_provisioning/scheme_softap.h"

static const char *TAG = "companion_wifi";
static EventGroupHandle_t wifi_events;
#define WIFI_CONNECTED_BIT BIT0
static int retry_count;
static bool provisioning_finished;
static esp_timer_handle_t reconnect_timer;

static void reconnect_timer_callback(void *arg)
{
    (void)arg;
    esp_wifi_connect();
}

static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(wifi_events, WIFI_CONNECTED_BIT);
        if (provisioning_finished && reconnect_timer) {
            uint32_t delay_ms = 1000U << (retry_count < 5 ? retry_count : 5);
            if (delay_ms > 30000U) delay_ms = 30000U;
            if (retry_count < 6) retry_count++;
            esp_timer_stop(reconnect_timer);
            esp_timer_start_once(reconnect_timer, (uint64_t)delay_ms * 1000ULL);
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        retry_count = 0;
        xEventGroupSetBits(wifi_events, WIFI_CONNECTED_BIT);
        ESP_LOGI(TAG, "Wi-Fi connected; cloud services can be used");
    } else if (base == WIFI_PROV_EVENT && id == WIFI_PROV_CRED_SUCCESS) {
        ESP_LOGI(TAG, "Wi-Fi credentials accepted");
    } else if (base == WIFI_PROV_EVENT && id == WIFI_PROV_CRED_FAIL) {
        ESP_LOGW(TAG, "Wi-Fi credentials rejected; retry provisioning after reboot/reset");
    } else if (base == WIFI_PROV_EVENT && id == WIFI_PROV_END) {
        provisioning_finished = true;
        wifi_prov_mgr_deinit();
    }
}

esp_err_t companion_wifi_start(char *setup_info, size_t setup_info_size)
{
    if (!setup_info || setup_info_size < 96) return ESP_ERR_INVALID_ARG;
    wifi_events = xEventGroupCreate();
    if (!wifi_events) return ESP_ERR_NO_MEM;
    esp_timer_create_args_t timer_args = {.callback = reconnect_timer_callback, .name = "wifi_reconnect"};
    ESP_RETURN_ON_ERROR(esp_timer_create(&timer_args, &reconnect_timer), TAG, "reconnect timer");
    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "netif init");
    ESP_RETURN_ON_ERROR(esp_event_loop_create_default(), TAG, "event loop init");
    esp_netif_create_default_wifi_sta();
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t wifi_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&wifi_cfg), TAG, "wifi init");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL), TAG, "wifi events");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, NULL), TAG, "ip events");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(WIFI_PROV_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL), TAG, "provision events");

    wifi_prov_mgr_config_t prov_cfg = {
        .scheme = wifi_prov_scheme_softap,
        .scheme_event_handler = WIFI_PROV_EVENT_HANDLER_NONE
    };
    ESP_RETURN_ON_ERROR(wifi_prov_mgr_init(prov_cfg), TAG, "provision manager init");
    bool provisioned = false;
    ESP_RETURN_ON_ERROR(wifi_prov_mgr_is_provisioned(&provisioned), TAG, "provision state");
    if (provisioned) {
        ESP_LOGI(TAG, "Stored Wi-Fi credentials found; starting station");
        wifi_prov_mgr_deinit();
        provisioning_finished = true;
        ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "station mode");
        ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "station start");
        ESP_RETURN_ON_ERROR(esp_wifi_connect(), TAG, "station connect");
        setup_info[0] = '\0';
        return ESP_OK;
    }

    uint8_t mac[6] = {0};
    ESP_RETURN_ON_ERROR(esp_read_mac(mac, ESP_MAC_WIFI_STA), TAG, "read MAC");
    const uint32_t pin = 10000000U + (esp_random() % 90000000U);
    char pop[16];
    snprintf(pop, sizeof(pop), "%08lu", (unsigned long)pin);
    char service_name[24], service_key[16];
    snprintf(service_name, sizeof(service_name), "AIC-%02X%02X%02X", mac[3], mac[4], mac[5]);
    snprintf(service_key, sizeof(service_key), "AC%08lu", (unsigned long)pin);
    snprintf(setup_info, setup_info_size, "Wi-Fi: %s\nPassword: %s\nPIN: %s", service_name, service_key, pop);
    ESP_LOGI(TAG, "Wi-Fi setup AP: %s / password: %s / PIN: %s", service_name, service_key, pop);
    ESP_RETURN_ON_ERROR(wifi_prov_mgr_start_provisioning(WIFI_PROV_SECURITY_1, pop, service_name, service_key), TAG, "start secure SoftAP provisioning");
    return ESP_OK;
}

bool companion_wifi_is_connected(void)
{
    return wifi_events && (xEventGroupGetBits(wifi_events) & WIFI_CONNECTED_BIT) != 0;
}
