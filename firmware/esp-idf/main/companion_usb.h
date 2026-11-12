#pragma once

#include "esp_err.h"

typedef void (*companion_usb_command_handler_t)(const char *command);

esp_err_t companion_usb_start(companion_usb_command_handler_t handler);
