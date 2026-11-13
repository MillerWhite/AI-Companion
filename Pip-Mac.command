#!/bin/bash
# Pip's panel: flash the keychain, set it up. Opens docs/index.html in a browser that can talk to USB (Chrome, Edge...).
cd "$(dirname "$0")"
PAGE="$PWD/docs/index.html"
for app in "Google Chrome" "Microsoft Edge" "Chromium" "Brave Browser" "Arc" "Opera"; do
    if open -Ra "$app" 2>/dev/null; then open -a "$app" "$PAGE"; exit 0; fi
done
osascript -e 'display alert "Нужен Google Chrome" message "Safari не умеет прошивать брелок. Скачай Chrome (откроется сайт) и запусти этот файл ещё раз."'
open "https://www.google.com/chrome/"
