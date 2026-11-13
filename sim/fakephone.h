/* A pretend iPhone for the simulators: it feeds phone.h like the BLE link will, and answers the keychain's commands. */
#pragma once

void fake_phone_init(void);
/* kind: 'L' link (0 off, 1 pairing, 2 on), 'N' the next sample notification, 'C' call (1 rings, 0 hangs up),
   'M' music (1 plays the sample album, 0 stops) */
void fake_phone(char kind, int arg);
const char *fake_phone_last(void); /* the last command the keychain sent, "" if none */
int fake_phone_shots(void);        /* photos taken with the shutter */
void fake_host(int arg);              /* the fake computer: 0 gone, 1 asks to pair, 2 paired and back */
const char *fake_host_report(void); /* "host=on clicks=.. prevs=.. mx=.. my=..": what the remote sent it */
