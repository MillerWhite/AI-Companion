#!/bin/bash
# Pip's panel: flash the keychain, set it up. Opens docs/index.html in Chrome, Chromium or Edge (they can talk to USB).
# The browser needs access to the keychain's port: once, run  sudo usermod -aG dialout $USER  and log in again.
cd "$(dirname "$0")"
PAGE="file://$PWD/docs/index.html"
for b in google-chrome google-chrome-stable chromium chromium-browser microsoft-edge brave-browser; do
    if command -v "$b" >/dev/null; then "$b" "$PAGE" >/dev/null 2>&1 & exit 0; fi
done
echo "Нужен Google Chrome или Chromium: Firefox не умеет прошивать брелок." >&2
xdg-open "https://www.google.com/chrome/" >/dev/null 2>&1
