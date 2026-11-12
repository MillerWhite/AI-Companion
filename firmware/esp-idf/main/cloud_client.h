#pragma once

#include <stddef.h>
#include "esp_err.h"

esp_err_t companion_cloud_interpret(const char *utterance,
                                    char *response_json,
                                    size_t response_capacity);
