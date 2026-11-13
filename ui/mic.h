/* The microphone: a sound level meter in dB and "someone is blowing on Pip". Pure C, no platform code:
   the host feeds it samples (firmware: the ES7210; simulator: made-up noise). */
#pragma once
#include <stdbool.h>
#include <stdint.h>

void mic_feed(const int16_t *s, int n, int rate);
bool mic_on(void);       /* samples are coming */
float mic_db(void);      /* about dB SPL, fast (125 ms) */
bool mic_blowing(void);  /* loud, low and steady: breath on the mic */
