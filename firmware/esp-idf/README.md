# AI Companion firmware — Waveshare ESP32-S3-Touch-AMOLED-1.75C

Firmware target: Waveshare ESP32-S3-Touch-AMOLED-1.75C (ESP32-S3, 32 MB flash, 8 MB PSRAM, 466×466 AMOLED). The source tree now contains the integrated service path; it is not yet a hardware-verified release.

## Firmware modules

| Module | Responsibility |
|---|---|
| `main.c` | LVGL face, touch reactions, autonomous timer, speech and IMU tasks, cloud intent application |
| `companion_emotion.c` | Needs, trust/playfulness progression, temperament-dependent emotional decisions |
| `companion_audio.c` | Waveshare BSP ES8311/ES7210 codec handles, soft procedural nonverbal cues, VAD and 16 kHz PCM capture |
| `companion_imu.c` | QMI8658 accelerometer, smoothing, motion/tilt gesture cooldowns |
| `companion_wifi.c` | Secure SoftAP provisioning and reconnect with exponential backoff |
| `companion_setup.c` | Local owner portal, activation claim, personality controls, memory export/reset |
| `cloud_client.c` | HTTPS SpeechKit upload, intent request, schema/allowlist validation, one-time claim and owner API proxy |

## First boot

1. Flash the firmware with the installer page.
2. On first boot the screen shows SoftAP SSID, password and security PIN. Use Espressif's ESP SoftAP Provisioning app to send the home Wi-Fi credentials.
3. After Wi-Fi connects, the screen shows the device's local IP and a rotating eight-digit owner-portal PIN.
4. Open `http://<device-ip>` from a browser on the same Wi-Fi. Enter the local PIN, backend HTTPS URL, companion name and one-time activation grant.
5. The device redeems the grant at `POST /api/devices/claim`; the device token is stored in NVS and is never returned to the browser.

The local portal uses HTTP on the private LAN and requires the PIN displayed on the physical device. Do not expose port 80 to the public internet. The activation grant is single-use and short-lived.

## Voice and cloud path

- ES7210 mic input is captured as stereo 16-bit PCM at 16 kHz; the firmware downmixes to mono.
- A basic RMS voice-activity detector collects up to six seconds and stops after silence.
- Mono LPCM is sent to `POST /api/device/transcribe` with the device Bearer token.
- Returned text is sent to `POST /api/device/interpret`.
- The firmware rejects unknown actions/emotions, invalid confidence and missing address-detection booleans. Only addressed, understood requests change the pet's behavior.
- If Wi-Fi/cloud services fail, the local face, needs, touch, autonomous behavior and sensor reactions continue. Cloud speech understanding is unavailable offline.
- No TTS or spoken audio is generated.

The VAD threshold, microphone channel selection, echo handling and audio levels need calibration on the physical board. Playback is deliberately low amplitude and throttled, but it is not a professionally mastered sound library.

## Wi-Fi

The provisioning manager uses SoftAP + Security 1 proof-of-possession. Wi-Fi credentials are stored by the ESP-IDF provisioning manager in NVS. Reconnect attempts use bounded exponential backoff and continue after repeated failures. To clear saved Wi-Fi credentials and re-enter provisioning, hold the face touch area; the device restarts into setup mode.

## Audio and sensors

Audio uses the Waveshare BSP codec factory handles and the reference close/reopen codec configuration sequence at 16 kHz, stereo, 16-bit. The QMI8658 managed component is configured for ±8 g and 500 Hz; filtered motion and tilt events have cooldowns.

Exact codec and sensor behavior, gain staging, microphone slot mapping, amplifier control and power draw still require a real-board test.

## Build

Requires ESP-IDF 5.5.x, the ESP-IDF component manager and network access to resolve the pinned Waveshare BSP, QMI8658 and LVGL components.

```bash
cd firmware/esp-idf
idf.py set-target esp32s3
idf.py build
idf.py -p COMx flash monitor
```

GitHub Actions workflow `.github/workflows/firmware-build.yml` attempts this build and packages a merged binary for ESP Web Tools. A tagged release `firmware-vX.Y.Z` publishes the binary used by `web/installer/manifest.json`.

## Not yet verified

The Waveshare board has not been physically tested in this development session. No successful ESP-IDF build, firmware flash, mic capture, speaker playback, Wi-Fi provisioning, IMU read, current draw, thermal or long-run test is claimed. The first successful CI build and a physical smoke test are release gates.
