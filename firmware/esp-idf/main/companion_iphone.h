#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

typedef enum {
    COMPANION_IPHONE_PLAY = 0,
    COMPANION_IPHONE_PAUSE = 1,
    COMPANION_IPHONE_TOGGLE_PLAY_PAUSE = 2,
    COMPANION_IPHONE_NEXT_TRACK = 3,
    COMPANION_IPHONE_PREVIOUS_TRACK = 4,
    COMPANION_IPHONE_VOLUME_UP = 5,
    COMPANION_IPHONE_VOLUME_DOWN = 6
} companion_iphone_command_t;

void companion_iphone_init(void);
bool companion_iphone_connected(void);
bool companion_iphone_ancs_ready(void);
bool companion_iphone_ams_ready(void);
esp_err_t companion_iphone_media_command(companion_iphone_command_t command);
void companion_iphone_get_notification(char *out, size_t out_len);
void companion_iphone_get_media(char *title, size_t title_len,
                                char *artist, size_t artist_len,
                                char *player, size_t player_len,
                                uint8_t *playback_state);
