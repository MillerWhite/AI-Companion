#!/bin/bash
# Pip debug page: open http://localhost:8765 (stop it before flashing). Needs pyserial: the ESP-IDF env has it.
source ~/esp/esp-idf/export.sh >/dev/null 2>&1 || true
exec python3 "$(dirname "$0")/pipdebug.py"
