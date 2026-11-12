#pragma once

#include <stddef.h>
#include <stdint.h>
#include "esp_http_client.h"
#include "esp_err.h"

esp_err_t companion_cloud_interpret(const char *utterance,
                                    char *response_json,
                                    size_t response_capacity);

esp_err_t companion_cloud_transcribe(const int16_t *mono_pcm,
                                     size_t sample_count,
                                     char *transcript,
                                     size_t transcript_capacity);
esp_err_t companion_cloud_claim(const char *api_base,
                                const char *serial,
                                const char *companion_name,
                                const char *enrollment_grant,
                                char *device_id,
                                size_t device_id_capacity);

esp_err_t companion_cloud_owner_request(esp_http_client_method_t method,
                                        const char *path,
                                        const char *json_body,
                                        char *response,
                                        size_t response_capacity);
