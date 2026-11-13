#!/bin/bash
# Builds the firmware and packs it for the panel (docs/index.html): docs/firmware/firmware.js holds every part of the
# image with the address it goes to, base64, so the page flashes it with nothing to install and from a local file too.
# The parts go to their own addresses (not one merged image): NVS between them - the pet, the settings, the phone's
# bond - stays as it is.
#   tools/release.sh            (needs ESP-IDF 5.5: . ~/esp/esp-idf/export.sh)
set -e
cd "$(dirname "$0")/.."
source ~/esp/esp-idf/export.sh >/dev/null 2>&1 || true
export KEYPET_PUBLIC=1 # nobody's private extras in it (private/)
(cd firmware && idf.py -B build-release build >/dev/null)
python3 - <<'EOF'
import base64, json, os, re, subprocess, time
build = "firmware/build-release"
args = open(os.path.join(build, "flash_args")).read().split()
parts = [{"address": int(a, 16), "file": f} for a, f in zip(args[6::2], args[7::2])]  # after the three --flash_* pairs
for p in parts:
    p["data"] = base64.b64encode(open(os.path.join(build, p.pop("file")), "rb").read()).decode()
ver = re.search(r'PROJECT_VER "([^"]+)"', open("firmware/CMakeLists.txt").read()).group(1)  # the version the board reports too
info = {"version": ver or "dev", "built": time.strftime("%Y-%m-%d"), "parts": parts}
os.makedirs("docs/firmware", exist_ok=True)
with open("docs/firmware/firmware.js", "w") as f:
    f.write("/* Pip's firmware for the panel: made by tools/release.sh, do not edit */\nwindow.PIP_FIRMWARE = ")
    json.dump(info, f)
    f.write(";\n")
size = sum(len(base64.b64decode(p["data"])) for p in parts)
print(f"docs/firmware/firmware.js: {ver}, {len(parts)} parts, {size // 1024} KB")
EOF
