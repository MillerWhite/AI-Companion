#pragma once

#include "esp_err.h"

typedef void (*companion_usb_command_handler_t)(const char *command);

esp_err_t companion_usb_start(companion_usb_command_handler_t handler);
void companion_usb_emit_emotion(const char *emotion);
void companion_usb_emit_touch(const char *event_name);
