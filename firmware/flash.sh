#!/bin/bash
# flash.sh [seconds]: build, flash, then print the log for a while (default 13 s). Errors only from the build.
cd "$(dirname "$0")"
source ~/esp/esp-idf/export.sh >/dev/null 2>&1
idf.py build 2>&1 | grep -E "error:|warning: " | sed -E 's/^.*(error|warning): /\1: /' | head -10
[ "${PIPESTATUS[0]}" -ne 0 ] && exit 1
PORT=$(ls /dev/cu.usbmodem* 2>/dev/null | head -1)
idf.py -p "$PORT" -b 921600 flash 2>&1 | grep -E "error|Error|Hard reset" | head -3
python serlog.py "${1:-13}" --reset --port "$PORT"
