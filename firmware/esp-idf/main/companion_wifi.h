#pragma once
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

esp_err_t companion_wifi_start(char *setup_info, size_t setup_info_size);
bool companion_wifi_is_connected(void);
