#include "cloud_client.h"

#include <stdio.h>
#include <string.h>
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "cJSON.h"

#define CLOUD_TIMEOUT_MS 15000

static const char *TAG = "companion_cloud";

typedef struct {
    char *buffer;
    size_t capacity;
    size_t length;
} response_buffer_t;

static esp_err_t read_nvs_string(const char *key, char *out, size_t capacity)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open("companion", NVS_READONLY, &handle);
    if (err != ESP_OK) return err;
    size_t required = capacity;
    err = nvs_get_str(handle, key, out, &required);
    nvs_close(handle);
    if (err == ESP_OK && required > capacity) return ESP_ERR_INVALID_SIZE;
    return err;
}

static esp_err_t on_http_event(esp_http_client_event_t *event)
{
    response_buffer_t *target = (response_buffer_t *)event->user_data;
    if (event->event_id == HTTP_EVENT_ON_DATA && target && event->data_len > 0) {
        const size_t incoming = (size_t)event->data_len;
        if (incoming >= target->capacity - target->length) return ESP_ERR_NO_MEM;
        memcpy(target->buffer + target->length, event->data, incoming);
        target->length += incoming;
        target->buffer[target->length] = '\0';
    }
    return ESP_OK;
}

esp_err_t companion_cloud_interpret(const char *utterance,
                                    char *response_json,
                                    size_t response_capacity)
{
    if (!utterance || !response_json || response_capacity < 2) return ESP_ERR_INVALID_ARG;
    response_json[0] = '\0';

    char api_base[192] = {0};
    char device_token[160] = {0};
    esp_err_t err = read_nvs_string("api_base", api_base, sizeof(api_base));
    if (err != ESP_OK) return ESP_ERR_INVALID_STATE;
    err = read_nvs_string("device_token", device_token, sizeof(device_token));
    if (err != ESP_OK || device_token[0] == '\0') return ESP_ERR_INVALID_STATE;

    size_t base_len = strlen(api_base);
    while (base_len > 0 && api_base[base_len - 1] == '/') api_base[--base_len] = '\0';
    char url[256];
    int url_len = snprintf(url, sizeof(url), "%s/api/device/interpret", api_base);
    if (url_len < 0 || (size_t)url_len >= sizeof(url)) return ESP_ERR_INVALID_SIZE;

    cJSON *body = cJSON_CreateObject();
    if (!body) return ESP_ERR_NO_MEM;
    if (!cJSON_AddStringToObject(body, "utterance", utterance)) {
        cJSON_Delete(body);
        return ESP_ERR_NO_MEM;
    }
    char *body_json = cJSON_PrintUnformatted(body);
    cJSON_Delete(body);
    if (!body_json) return ESP_ERR_NO_MEM;

    response_buffer_t target = {.buffer = response_json, .capacity = response_capacity, .length = 0};
    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = CLOUD_TIMEOUT_MS,
        .event_handler = on_http_event,
        .user_data = &target,
        .crt_bundle_attach = esp_crt_bundle_attach
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        cJSON_free(body_json);
        return ESP_FAIL;
    }

    char authorization[192];
    int auth_len = snprintf(authorization, sizeof(authorization), "Bearer %s", device_token);
    if (auth_len < 0 || (size_t)auth_len >= sizeof(authorization)) {
        esp_http_client_cleanup(client);
        cJSON_free(body_json);
        return ESP_ERR_INVALID_SIZE;
    }

    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_header(client, "Authorization", authorization);
    esp_http_client_set_post_field(client, body_json, (int)strlen(body_json));
    err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    cJSON_free(body_json);

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Cloud request failed: %s", esp_err_to_name(err));
        return err;
    }
    if (status != 200) {
        ESP_LOGW(TAG, "Cloud backend returned HTTP %d", status);
        return ESP_FAIL;
    }
    if (target.length == 0) return ESP_ERR_INVALID_RESPONSE;

    /* Treat cloud JSON as untrusted data. Reject missing or unknown enum values
       before a future behavior layer can apply the result to the display. */
    cJSON *result = cJSON_ParseWithLength(response_json, target.length);
    if (!result) return ESP_ERR_INVALID_RESPONSE;
    const cJSON *addressed = cJSON_GetObjectItemCaseSensitive(result, "addressed_to_companion");
    const cJSON *understood = cJSON_GetObjectItemCaseSensitive(result, "understood");
    const cJSON *action = cJSON_GetObjectItemCaseSensitive(result, "action");
    const cJSON *emotion = cJSON_GetObjectItemCaseSensitive(result, "emotion");
    const cJSON *confidence = cJSON_GetObjectItemCaseSensitive(result, "confidence");
    const bool valid_action = cJSON_IsString(action) &&
        (strcmp(action->valuestring, "idle") == 0 ||
         strcmp(action->valuestring, "greet") == 0 ||
         strcmp(action->valuestring, "acknowledge") == 0 ||
         strcmp(action->valuestring, "play") == 0 ||
         strcmp(action->valuestring, "pet") == 0 ||
         strcmp(action->valuestring, "sleep") == 0 ||
         strcmp(action->valuestring, "wake") == 0 ||
         strcmp(action->valuestring, "surprise") == 0 ||
         strcmp(action->valuestring, "comfort") == 0 ||
         strcmp(action->valuestring, "attend") == 0 ||
         strcmp(action->valuestring, "express_joy") == 0 ||
         strcmp(action->valuestring, "express_curiosity") == 0 ||
         strcmp(action->valuestring, "thank") == 0 ||
         strcmp(action->valuestring, "clarify") == 0);
    const bool valid_emotion = cJSON_IsString(emotion) &&
        (strcmp(emotion->valuestring, "neutral") == 0 ||
         strcmp(emotion->valuestring, "happy") == 0 ||
         strcmp(emotion->valuestring, "curious") == 0 ||
         strcmp(emotion->valuestring, "affectionate") == 0 ||
         strcmp(emotion->valuestring, "sleepy") == 0 ||
         strcmp(emotion->valuestring, "surprised") == 0 ||
         strcmp(emotion->valuestring, "sad") == 0 ||
         strcmp(emotion->valuestring, "calm") == 0 ||
         strcmp(emotion->valuestring, "excited") == 0 ||
         strcmp(emotion->valuestring, "concerned") == 0);
    const bool valid = cJSON_IsBool(addressed) && cJSON_IsBool(understood) &&
        valid_action && valid_emotion && cJSON_IsNumber(confidence) &&
        confidence->valuedouble >= 0.0 && confidence->valuedouble <= 1.0;
    cJSON_Delete(result);
    if (!valid) {
        response_json[0] = '\\0';
        ESP_LOGW(TAG, "Cloud response did not match the intent allowlist");
        return ESP_ERR_INVALID_RESPONSE;
    }
    return ESP_OK;
}
