# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

KeyPet: firmware for a Waveshare ESP32-S3-Touch-AMOLED-1.75C keychain (466×466 round AMOLED, CO5300 over QSPI,
CST9217 touch, QMI8658 accelerometer, AXP2101 power, ES8311 speaker, ES7210 mics, 8 MB PSRAM, 32 MB flash).
The character is **Pip** (white eyes, moods driven by a tamagotchi model), plus a menu of apps and an iPhone link
over BLE. The owner speaks Russian: UI text, commit messages and replies are in Russian.

## Three targets, one UI

`ui/` is portable C + LVGL 9.5 and is compiled unchanged into:
- **headless recorder** `pipsim` (sim/main.c) — deterministic runs to PNG/GIF/MP4, used for every visual check;
- **web simulator** (sim/wasm.c → sim/web, Emscripten) — interactive, on a 3D model of the keychain;
- **firmware** (firmware/main, ESP-IDF 5.5) — main.c is the board glue (display port, sensors, power, audio),
  ble.c the iPhone link, debug.c the debug link.

When adding a `ui/*.c` file, add it to both `sim/CMakeLists.txt` (`set(UI ...)`) and
`firmware/main/CMakeLists.txt` (`SRCS`). Fonts in `ui/fonts/font_*_*.c` are globbed: after adding one, re-run cmake
(`cmake build`, `cmake build-web`, `idf.py reconfigure`).

## Commands

```
cmake -S sim -B build -G Ninja && cmake --build build          # pipsim + face_test/voice_test/pet_test
python3 sim/check.py                                           # the smoke suite (must stay all "ok")
./build/pipsim --out out/x.png --secs 1.5 --events "0.05:H1 0.3:oqr 0.6:P"   # one scenario; events listed at the top of sim/main.c
emcmake cmake -S sim -B build-web -G Ninja && cmake --build build-web       # web sim -> sim/web/pip.{js,wasm}
python3 sim/serve.py                                           # web sim on :8466 (also .claude/launch.json "keypet-sim")

firmware/flash.sh [secs]          # idf.py build + flash + print the log for N s (finds /dev/cu.usbmodem* itself)
python firmware/serlog.py 20 [--reset]   # just the log (needs the IDF env: . ~/esp/esp-idf/export.sh)
tools/pipdebug.sh                 # debug page http://localhost:8765 (USB, or the page's Bluetooth button in Chrome)
```

- A single check is just a pipsim run: the last stdout line is `status: mood=… reaction=… screen=… theme=…`;
  `sim/check.py` is a list of such runs with asserts. Theme in events: `H1` Приборка (default), `H3` Диагностика.
- The debug server holds the USB port: stop it before `flash.sh`.
- Editing `firmware/sdkconfig.defaults` does nothing until `firmware/sdkconfig` is deleted (or edit both).
- Generators: `tools/fonts.sh` (theme fonts from assets/fonts), `tools/release.sh` (the public firmware for the panel:
  `docs/firmware/firmware.js`, built with `KEYPET_PUBLIC=1` in `firmware/build-release`).
- Public vs the owner's build: `private/` is gitignored; if `private/aitu_mark.c` exists the sim and firmware add it and
  define `KEYPET_AITU` (the AITU mark on the badge). Without it the badge draws Pip's eyes. Nothing personal goes in git:
  the badge name and QR links are `ui/profile.c` defaults, set per keychain from the panel and kept in NVS.

## The panel (what end users get)

`docs/index.html`: connect (Web Serial, VID 0x303A, or Web Bluetooth), settings (badge, QR links, sleep timer,
brightness, theme, features, notification privacy) and flashing with esptool-js (parts at their own addresses, so NVS -
pet, settings, bonds - survives). Works as a plain file (`Pip-Windows.bat` / `Pip-Mac.command` / `Pip-Linux.sh` open it
in Chrome/Edge) and from GitHub Pages. `docs/debug.html` is the developer page (USB/Bluetooth buttons, or served by
`tools/pipdebug.py`). Board commands for settings: `name first|last|org`, `link i service|handle|url`, `power s`,
`bright %`, `theme i`, `privacy i`, `feat i 0|1`, `config` (answers `@C {json}`); all persisted by main.c in NVS.

## Architecture worth knowing before editing

**Pip** (`ui/pip.c`): moods are a table of eye shapes + behaviour (`moods[]`); every frame springs chase the
target shape, `face.c` rasterizes the two eyes. Base mood comes from `pet.c` (`pet_mood_why`, first match wins:
asleep → a rare card (stoned/pinkeye, ~once a month) → the body (battery) → an aftertaste of an event (won, lost,
fright, notification flood, sulking…) → a need under its line (near-equal needs take turns, a long spell shows the
urge's other face) → tired night → a situation (just woke, walking, charging) → the day's palette by valence, calmer
in the evening, weighted, never the same twice running). `pet_settle()` remembers what it showed (palette hysteresis,
how long an urge has held). Every threshold is a knob in `pet_tune`, named in `pet_knobs[]` (debug `set`, pipsim
`K<name>=<v>`). Reactions (`react()`) override it for seconds. Touch, IMU (`imu.c`: tilt, shake, face-down, upside
down, free fall, steps), mic (`mic.c`: dB, blowing; a bang in `pip.c`), battery, the phone (`pip_phone`, `pip_note`),
games (`pip_game`), `pip_seen()` and `pip_wake_up()` (power-on intro) feed it. `pip_feature()` switches inputs off.
Left alone for `pip_power()` seconds (default 30; touch, buttons, lifting him, notifications count) the screen dims, then sleeps; apps with `.awake` hold it on. Face down for 1 s the screen sleeps (`pip_display_on()`: main.c turns the panel off, pip.c stops drawing, pipsim
renders black and prints `display=0`); turning it over or any button wakes it (that press does nothing else).

**Shell** (`ui/shell.c`): Pip ⇄ menu ⇄ apps, side buttons (top = open, bottom = back/meet). `apps[]` in
`ui/apps.c`: the first `app_count` are the menu; apps with `.group` (the games) follow and are shown by the
list menu when their owner row ("games") is opened. An app gets `.imu` (accelerometer, g) and `.gyro` (deg/s,
QMI8658 on the board, event `g` in pipsim) while it is open. The maze's physics is plain C in `ui/maze.c`
(`maze_test` checks it). Menu styles: list wheel (Приборка/Диагностика), honeycomb and
carousel (themes hidden today). List rows are pre-rendered A8 masks at every size (rows_build/shrink) — never scale
text live, it costs ~45 ms a frame on the board.

**Themes** (`ui/theme.c`): `th->…` colours/fonts; `TH_TERMINAL` (Диагностика) draws with a cell kit
(`th_cells`, `th_header` with cursor, `th_plate`), the other themes must not show those terminal elements.
The chosen theme persists in NVS (main.c `theme_load/theme_keep`).

**iPhone** (`ui/phone.c` UI model ↔ `firmware/main/ble.c`, `sim/fakephone.c` in the sim): NimBLE peripheral
advertising as a HID keyboard "Pip"; ANCS notifications/calls, AMS music, CTS time, HID volume-up = camera shutter.
Three connections: the phone, the debug page (whoever writes the debug service within 4 s is the page) and a computer
(it bonds without ANCS/AMS; known computers' addresses are kept in NVS). The computer gets a second HID service,
appended last so the iPhone's cached handles stay valid: mouse + keyboard + media keys, but its report map holds the
keyboard only for the computer's connection (iOS hides its on-screen keyboard if it sees one). Remote app: top =
Page Down, bottom tap = Page Up, bottom held + tilt = wheel (`WHEEL_SIGN` assumes macOS natural scrolling), a finger
held + the gyroscope = pointer, a tap = click. The music app sends media keys to the computer when one is connected
(a tap switches to the iPhone). The bottom key's release reaches apps as `shell_button_up` (`.button_up`).
In the sim the computer is `fakephone.c` too (event `X0/1/2`, `B<s>` holds the bottom key; the status ends with
`host=… nexts=… prevs=… clicks=… mx=… my=… scroll=… media=…`).

**Board display port** (firmware/main/main.c): own LVGL port, DIRECT mode into one PSRAM frame buffer
(RGB565_SWAPPED), flush waits for the panel's TE pulse and streams dirty rows in 8 KB DMA chunks at 80 MHz QSPI;
LVGL on core 0, the face renderer splits rows across both cores (hot loops in IRAM, `-O2`). Pip's canvas is drawn in
the panel's byte order (`face_t.swapped`), so LVGL copies it with a plain memcpy; pipsim's display is RGB565_SWAPPED
too, so the sim runs the same paths. In `face.c` keep libm (`sinf`, `atan2f`, `sqrtf`, `fminf`…) out of per-pixel code:
use `sin_fast`/`atan2_fast`/`sqrt_fast`/`mn`/`mx`.
The status log every 5 s prints fps, render ms, face ms, free internal/PSRAM and the worst frame of those 5 s with
what was on screen (`pip_frame_worst`; also `fw` in `@S`) — check it after visual changes.

**Debug link** (firmware/main/debug.c ↔ tools/pipdebug.*): `@S` JSON status each second, `@H` mood history
(a minute per row, a flash ring in the `storage` partition, survives reboots), text commands in
(`mood`, `react`, `need`, `urge`, `set`, `rare`, `game`, `feat`, `demo`, `hist`, `open`, `btn`…); the knobs go out as
`@K` lines then `@T`. A new branch of the tree needs its `why` text in tools/pipdebug.html and a check in sim/check.py.

## Board constraints that bite

- Internal RAM is the scarce one (~80 KB free); allocations ≥ 2 KB go to PSRAM automatically
  (`CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=2048`). Never use `__thread` for buffers (it broke boot).
- No FPU divide/sqrt on the ESP32-S3: avoid them in per-pixel loops (see face.c, the dice rasterizer in apps.c).
- Big LVGL circles/transforms/opacity layers are slow; prefer pre-rendered images (cup, grid background, QR codes).
- Dark greys in RGB565 tint green; quantize channels to 5 bits (see the dice cup) or use multiples of 8.
- Measure on the board before claiming performance; the sim is not representative.

## Conventions

- Verify visually with pipsim renders (contact sheets via PIL) and keep `sim/check.py` green; extend it when
  adding reactions/apps (`app_ids` list).
- Commit messages in Russian, summary line + short bullet list.
- macOS `seq 1 0` counts down — careful in shell loops building event strings.
