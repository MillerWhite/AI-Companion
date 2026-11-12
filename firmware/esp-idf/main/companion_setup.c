#include "companion_setup.h"

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "cJSON.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "nvs.h"
#include "cloud_client.h"

static const char *TAG = "companion_setup";
static httpd_handle_t server;

static bool setup_pin_valid(httpd_req_t *req)
{
    char supplied[24] = {0}, expected[24] = {0};
    if (httpd_req_get_hdr_value_str(req, "X-Setup-Pin", supplied, sizeof(supplied)) != ESP_OK) return false;
    nvs_handle_t handle;
    if (nvs_open("companion", NVS_READONLY, &handle) != ESP_OK) return false;
    size_t size = sizeof(expected);
    esp_err_t err = nvs_get_str(handle, "setup_pin", expected, &size);
    nvs_close(handle);
    return err == ESP_OK && strlen(supplied) == 8 && strcmp(supplied, expected) == 0;
}

static esp_err_t root_handler(httpd_req_t *req)
{
    static const char page[] =
        "<!doctype html><html lang='ru'><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>AI Companion setup</title><style>body{margin:0;background:#08090d;color:#f4f4f5;font:16px system-ui;display:grid;place-items:center;min-height:100vh}"
        "main{width:min(92vw,430px);padding:26px;background:#151820;border:1px solid #303744;border-radius:20px}h1{font-size:23px}p{color:#aab2c0;font-size:13px;line-height:1.5}"
        "input,select{box-sizing:border-box;width:100%;padding:13px;margin:6px 0 13px;background:#090b10;color:white;border:1px solid #343c49;border-radius:10px}"
        "button{width:100%;padding:14px;background:#d9f4ff;color:#08121a;border:0;border-radius:12px;font-weight:700}small{color:#9ba5b5}</style>"
        "<main><h1>Настройка AI Companion</h1><p>Устройство уже подключено к Wi-Fi. Вставьте одноразовый код активации, полученный после покупки. Токен устройства будет сохранён внутри него и не покажется в браузере.</p>"
        "<label>Адрес backend</label><input id='api' value='https://api.example.com' placeholder='https://ваш-backend'>"
        "<label>Серийный номер</label><input id='serial' readonly>"
        "<label>PIN с экрана устройства</label><input id='pin' type='password' inputmode='numeric' maxlength='8' placeholder='8 цифр'>"\n        "<label>Имя компаньона</label><input id='name' maxlength='40' value='Milo'>"
        "<label>Одноразовый код активации</label><input id='grant' autocomplete='off' placeholder='Код активации'>"
        "<button id='claim'>Активировать устройство</button><p id='status'>Подключитесь к устройству в той же Wi-Fi сети.</p>"
        "<section id='owner' hidden><hr><h2>Управление компаньоном</h2><label>Темперамент</label><select id='temper'><option value='balanced'>Сбалансированный</option><option value='calm'>Спокойный</option><option value='energetic'>Энергичный</option><option value='shy'>Застенчивый</option><option value='dreamy'>Мечтательный</option></select>"
        "<label>Инициативность</label><select id='initiative'><option value='medium'>Средняя</option><option value='high'>Высокая</option><option value='low'>Низкая</option></select>"
        "<button id='profile'>Сохранить характер</button><button id='memory'>Посмотреть память</button><button id='reset'>Сбросить серверную память</button><pre id='memoryData' style='white-space:pre-wrap;color:#aab2c0;font-size:12px'></pre></section>"
        "<small>Не отправляйте код активации другим людям. Устройство хранит секрет авторизации в защищённом NVS, браузер его не получает.</small></main>"
        "<script>const $=s=>document.querySelector(s);fetch('/device-info').then(r=>r.json()).then(d=>{$('#serial').value=d.serial;$('#status').textContent='Устройство онлайн · '+d.ip+(d.claimed?' · уже активировано':' · ожидает активации');if(d.claimed){$('#owner').hidden=false;$('#name').value=d.companion_name||'Milo';$('#temper').value=d.temperament||'balanced';$('#initiative').value=d.initiative||'medium'}}).catch(()=>{});"
        "$('#claim').onclick=async()=>{let b=$('#claim'),s=$('#status');b.disabled=true;s.textContent='Активируем…';try{let r=await fetch('/claim',{method:'POST',headers:{'Content-Type':'application/json','X-Setup-Pin':$('#pin').value.trim()},body:JSON.stringify({api_base:$('#api').value.trim(),serial:$('#serial').value,companion_name:$('#name').value.trim(),enrollment_grant:$('#grant').value.trim()})});let d=await r.json();if(!r.ok)throw Error(d.message||d.detail?.message||'Ошибка активации');s.textContent='Готово. Устройство активировано.';$('#grant').value='';$('#owner').hidden=false;}catch(e){s.textContent='Не удалось активировать: '+e.message;}finally{b.disabled=false}};"
        "$('#profile').onclick=async()=>{let r=await fetch('/profile',{method:'POST',headers:{'Content-Type':'application/json','X-Setup-Pin':$('#pin').value.trim()},body:JSON.stringify({companion_name:$('#name').value.trim(),personality:{temperament:$('#temper').value,initiative:$('#initiative').value}})});$('#status').textContent=r.ok?'Характер сохранён':'Не удалось сохранить характер'};"
        "$('#memory').onclick=async()=>{let r=await fetch('/memory',{headers:{'X-Setup-Pin':$('#pin').value.trim()}}),d=await r.json();$('#memoryData').textContent=JSON.stringify(d,null,2)};"
        "$('#reset').onclick=async()=>{if(!confirm('Удалить серверную память и историю отношений?'))return;let r=await fetch('/memory/reset',{method:'POST',headers:{'X-Setup-Pin':$('#pin').value.trim()}});$('#memoryData').textContent=r.ok?'Серверная память удалена':'Не удалось удалить память'};</script></html>";
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
    if (!json) return ESP_ERR_NO_MEM;
    cJSON_AddStringToObject(json, "serial", serial);
    cJSON_AddStringToObject(json, "ip", ip_text);
    cJSON_AddBoolToObject(json, "claimed", claimed);
    if (nvs_open("companion", NVS_READONLY, &nvs) == ESP_OK) {
        char companion_name[48] = "Milo"; size_t name_size = sizeof(companion_name);
        if (nvs_get_str(nvs, "companion_name", companion_name, &name_size) == ESP_OK)
            cJSON_AddStringToObject(json, "companion_name", companion_name);
        uint8_t temperament = 0, initiative = 0;
        nvs_get_u8(nvs, "temperament", &temperament);
        nvs_get_u8(nvs, "initiative", &initiative);
        static const char *temper_names[] = {"balanced", "calm", "energetic", "shy", "dreamy"};
        static const char *initiative_names[] = {"medium", "high", "low"};
        cJSON_AddStringToObject(json, "temperament", temper_names[temperament <= 4 ? temperament : 0]);
        cJSON_AddStringToObject(json, "initiative", initiative_names[initiative <= 2 ? initiative : 0]);
        nvs_close(nvs);
    }
    char *body = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);
    if (!body) return ESP_ERR_NO_MEM;
    httpd_resp_set_type(req, "application/json");
    esp_err_t err = httpd_resp_send(req, body, HTTPD_RESP_USE_STRLEN);
    cJSON_free(body);
    return err;
}

static esp_err_t claim_handler(httpd_req_t *req)
{
    if (!setup_pin_valid(req)) {
        httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Setup PIN required");
        return ESP_FAIL;
    }

    if (req->content_len <= 0 || req->content_len > 4096) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid request size");
        return ESP_FAIL;
    }
    char body[4097] = {0};
    size_t total = 0;
    while (total < (size_t)req->content_len) {
        int received = httpd_req_recv(req, body + total, req->content_len - total);
        if (received <= 0) {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Request body incomplete");
            return ESP_FAIL;
        }
        total += (size_t)received;
    }
    cJSON *json = cJSON_ParseWithLength(body, total);
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
    if (!response) return ESP_ERR_NO_MEM;
    if (err == ESP_OK) {
        cJSON_AddBoolToObject(response, "ok", true);
        cJSON_AddStringToObject(response, "device_id", device_id);
    } else {
        cJSON_AddBoolToObject(response, "ok", false);
        cJSON_AddStringToObject(response, "message", "Сервер отклонил активацию. Проверьте одноразовый код и адрес backend.");
    }
    char *output = cJSON_PrintUnformatted(response);
    cJSON_Delete(response);
    if (!output) return ESP_ERR_NO_MEM;
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_status(req, err == ESP_OK ? "200 OK" : "400 Bad Request");
    esp_err_t send_err = httpd_resp_send(req, output, HTTPD_RESP_USE_STRLEN);
    cJSON_free(output);
    return send_err;
}


static esp_err_t owner_memory_handler(httpd_req_t *req)
{
    if (!setup_pin_valid(req)) {
        httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Setup PIN required");
        return ESP_FAIL;
    }

    char response[8192] = {0};
    esp_err_t err = companion_cloud_owner_request(HTTP_METHOD_GET, "/api/device/memory", NULL, response, sizeof(response));
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Owner memory service unavailable");
        return err;
    }
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t owner_memory_reset_handler(httpd_req_t *req)
{
    if (!setup_pin_valid(req)) {
        httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Setup PIN required");
        return ESP_FAIL;
    }

    char response[1024] = {0};
    esp_err_t err = companion_cloud_owner_request(HTTP_METHOD_DELETE, "/api/device/memory", NULL, response, sizeof(response));
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Memory reset failed");
        return err;
    }
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t owner_profile_handler(httpd_req_t *req)
{
    if (!setup_pin_valid(req)) {
        httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Setup PIN required");
        return ESP_FAIL;
    }

    if (req->content_len <= 0 || req->content_len > 2048) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid profile size");
        return ESP_FAIL;
    }
    char body[2049] = {0};
    size_t total = 0;
    while (total < (size_t)req->content_len) {
        int received = httpd_req_recv(req, body + total, req->content_len - total);
        if (received <= 0) {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Profile body incomplete");
            return ESP_FAIL;
        }
        total += (size_t)received;
    }
    cJSON *json = cJSON_ParseWithLength(body, total);
    if (!json) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid profile JSON");
        return ESP_FAIL;
    }
    char updated_name[48] = {0};
    const cJSON *name_item = cJSON_GetObjectItemCaseSensitive(json, "companion_name");
    if (cJSON_IsString(name_item)) strlcpy(updated_name, name_item->valuestring, sizeof(updated_name));
    const cJSON *personality = cJSON_GetObjectItemCaseSensitive(json, "personality");
    if (cJSON_IsObject(personality)) {
        const cJSON *temper = cJSON_GetObjectItemCaseSensitive(personality, "temperament");
        const cJSON *initiative = cJSON_GetObjectItemCaseSensitive(personality, "initiative");
        uint8_t temper_value = 0, initiative_value = 0;
        if (cJSON_IsString(temper)) {
            if (!strcmp(temper->valuestring, "calm")) temper_value = 1;
            else if (!strcmp(temper->valuestring, "energetic")) temper_value = 2;
            else if (!strcmp(temper->valuestring, "shy")) temper_value = 3;
            else if (!strcmp(temper->valuestring, "dreamy")) temper_value = 4;
        }
        if (cJSON_IsString(initiative)) {
            if (!strcmp(initiative->valuestring, "high")) initiative_value = 1;
            else if (!strcmp(initiative->valuestring, "low")) initiative_value = 2;
        }
        nvs_handle_t handle;
        if (nvs_open("companion", NVS_READWRITE, &handle) == ESP_OK) {
            nvs_set_u8(handle, "temperament", temper_value);
            nvs_set_u8(handle, "initiative", initiative_value);
            nvs_commit(handle);
            nvs_close(handle);
        }
    }
    char *payload = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);
    if (!payload) return ESP_ERR_NO_MEM;
    char response[2048] = {0};
    esp_err_t err = companion_cloud_owner_request(HTTP_METHOD_PATCH, "/api/device/profile", payload, response, sizeof(response));
    cJSON_free(payload);
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Profile update failed");
        return err;
    }
    if (updated_name[0]) {
        nvs_handle_t handle;
        if (nvs_open("companion", NVS_READWRITE, &handle) == ESP_OK) {
            nvs_set_str(handle, "companion_name", updated_name);
            nvs_commit(handle);
            nvs_close(handle);
        }
    }
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN);
}

esp_err_t companion_setup_server_start(void)
{
    if (server) return ESP_OK;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.max_uri_handlers = 10;
    esp_err_t err = httpd_start(&server, &config);
    if (err != ESP_OK) return err;
    httpd_uri_t root = {.uri = "/", .method = HTTP_GET, .handler = root_handler};
    httpd_uri_t info = {.uri = "/device-info", .method = HTTP_GET, .handler = device_info_handler};
    httpd_uri_t claim = {.uri = "/claim", .method = HTTP_POST, .handler = claim_handler};
    httpd_uri_t memory = {.uri = "/memory", .method = HTTP_GET, .handler = owner_memory_handler};
    httpd_uri_t memory_reset = {.uri = "/memory/reset", .method = HTTP_POST, .handler = owner_memory_reset_handler};
    httpd_uri_t profile = {.uri = "/profile", .method = HTTP_POST, .handler = owner_profile_handler};
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server, &root), TAG, "register root");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server, &info), TAG, "register info");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server, &claim), TAG, "register claim");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server, &memory), TAG, "register memory");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server, &memory_reset), TAG, "register memory reset");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server, &profile), TAG, "register profile");
    ESP_LOGI(TAG, "Local owner setup portal started on port 80");
    return ESP_OK;
}
