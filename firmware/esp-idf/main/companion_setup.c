#include "companion_setup.h"

#include <stdio.h>
#include <string.h>
#include "cJSON.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "nvs.h"
#include "cloud_client.h"

static const char *TAG = "companion_setup";
static httpd_handle_t server;

static esp_err_t root_handler(httpd_req_t *req)
{
    static const char page[] =
        "<!doctype html><html lang='ru'><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>AI Companion setup</title><style>body{margin:0;background:#08090d;color:#f4f4f5;font:16px system-ui;display:grid;place-items:center;min-height:100vh}"
        "main{width:min(92vw,430px);padding:26px;background:#151820;border:1px solid #303744;border-radius:20px}h1{font-size:23px}p{color:#aab2c0;font-size:13px;line-height:1.5}"
        "input{box-sizing:border-box;width:100%;padding:13px;margin:6px 0 13px;background:#090b10;color:white;border:1px solid #343c49;border-radius:10px}"
        "button{width:100%;padding:14px;background:#d9f4ff;color:#08121a;border:0;border-radius:12px;font-weight:700}small{color:#9ba5b5}</style>"
        "<main><h1>Настройка AI Companion</h1><p>Устройство уже подключено к Wi-Fi. Вставьте одноразовый код активации, полученный после покупки. Токен устройства будет сохранён внутри него и не покажется в браузере.</p>"
        "<label>Адрес backend</label><input id='api' value='https://api.example.com' placeholder='https://ваш-backend'>"
        "<label>Серийный номер</label><input id='serial' readonly>"
        "<label>Имя компаньона</label><input id='name' maxlength='40' value='Milo'>"
        "<label>Одноразовый код активации</label><input id='grant' autocomplete='off' placeholder='Код активации'>"
        "<button id='claim'>Активировать устройство</button><p id='status'>Подключитесь к устройству в той же Wi-Fi сети.</p><small>Не отправляйте код активации другим людям.</small></main>"
        "<script>fetch('/device-info').then(r=>r.json()).then(d=>{document.querySelector('#serial').value=d.serial;document.querySelector('#status').textContent='Устройство онлайн · '+d.ip+(d.claimed?' · уже активировано':' · ожидает активации')}).catch(()=>{});"
        "document.querySelector('#claim').onclick=async()=>{let b=document.querySelector('#claim'),s=document.querySelector('#status');b.disabled=true;s.textContent='Активируем…';try{let r=await fetch('/claim',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({api_base:document.querySelector('#api').value.trim(),serial:document.querySelector('#serial').value,companion_name:document.querySelector('#name').value.trim(),enrollment_grant:document.querySelector('#grant').value.trim()})});let d=await r.json();if(!r.ok)throw Error(d.message||d.detail?.message||'Ошибка активации');s.textContent='Готово. Устройство активировано. Можно закрыть эту страницу.';document.querySelector('#grant').value='';}catch(e){s.textContent='Не удалось активировать: '+e.message;}finally{b.disabled=false}};</script></html>";
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, page, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t device_info_handler(httpd_req_t *req)
{
    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    char serial[32], ip_text[24] = "unknown";
    snprintf(serial, sizeof(serial), "AIC-%02X%02X%02X%02X%02X%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    esp_netif_ip_info_t ip = {0};
    if (netif && esp_netif_get_ip_info(netif, &ip) == ESP_OK) {
        snprintf(ip_text, sizeof(ip_text), IPSTR, IP2STR(&ip.ip));
    }
    nvs_handle_t nvs;
    bool claimed = false;
    if (nvs_open("companion", NVS_READONLY, &nvs) == ESP_OK) {
        char token[160] = {0}; size_t size = sizeof(token);
        claimed = nvs_get_str(nvs, "device_token", token, &size) == ESP_OK && token[0] != '\0';
        nvs_close(nvs);
    }
    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "serial", serial);
    cJSON_AddStringToObject(json, "ip", ip_text);
    cJSON_AddBoolToObject(json, "claimed", claimed);
    char *body = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);
    httpd_resp_set_type(req, "application/json");
    esp_err_t err = httpd_resp_send(req, body, HTTPD_RESP_USE_STRLEN);
    cJSON_free(body);
    return err;
}

static esp_err_t claim_handler(httpd_req_t *req)
{
    if (req->content_len <= 0 || req->content_len > 4096) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid request size");
        return ESP_FAIL;
    }
    char body[4097] = {0};
    int received = httpd_req_recv(req, body, req->content_len);
    if (received <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Request body missing");
        return ESP_FAIL;
    }
    cJSON *json = cJSON_ParseWithLength(body, (size_t)received);
    if (!json) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }
    const cJSON *api = cJSON_GetObjectItemCaseSensitive(json, "api_base");
    const cJSON *serial = cJSON_GetObjectItemCaseSensitive(json, "serial");
    const cJSON *name = cJSON_GetObjectItemCaseSensitive(json, "companion_name");
    const cJSON *grant = cJSON_GetObjectItemCaseSensitive(json, "enrollment_grant");
    if (!cJSON_IsString(api) || !cJSON_IsString(serial) || !cJSON_IsString(name) || !cJSON_IsString(grant) ||
        strncmp(api->valuestring, "https://", 8) != 0 || strlen(api->valuestring) > 180 ||
        strlen(serial->valuestring) > 80 || strlen(name->valuestring) > 40 ||
        strlen(grant->valuestring) < 24 || strlen(grant->valuestring) > 160) {
        cJSON_Delete(json);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid setup fields; backend must use HTTPS");
        return ESP_FAIL;
    }
    char device_id[80] = {0};
    esp_err_t err = companion_cloud_claim(api->valuestring, serial->valuestring,
                                          name->valuestring, grant->valuestring,
                                          device_id, sizeof(device_id));
    cJSON_Delete(json);
    cJSON *response = cJSON_CreateObject();
    if (err == ESP_OK) {
        cJSON_AddBoolToObject(response, "ok", true);
        cJSON_AddStringToObject(response, "device_id", device_id);
    } else {
        cJSON_AddBoolToObject(response, "ok", false);
        cJSON_AddStringToObject(response, "message", "Сервер отклонил активацию. Проверьте одноразовый код и адрес backend.");
    }
    char *output = cJSON_PrintUnformatted(response);
    cJSON_Delete(response);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_status(req, err == ESP_OK ? "200 OK" : "400 Bad Request");
    esp_err_t send_err = httpd_resp_send(req, output, HTTPD_RESP_USE_STRLEN);
    cJSON_free(output);
    return send_err;
}

esp_err_t companion_setup_server_start(void)
{
    if (server) return ESP_OK;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.max_uri_handlers = 8;
    esp_err_t err = httpd_start(&server, &config);
    if (err != ESP_OK) return err;
    httpd_uri_t root = {.uri = "/", .method = HTTP_GET, .handler = root_handler};
    httpd_uri_t info = {.uri = "/device-info", .method = HTTP_GET, .handler = device_info_handler};
    httpd_uri_t claim = {.uri = "/claim", .method = HTTP_POST, .handler = claim_handler};
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server, &root), TAG, "register root");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server, &info), TAG, "register info");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server, &claim), TAG, "register claim");
    ESP_LOGI(TAG, "Local owner setup portal started on port 80");
    return ESP_OK;
}
