# AI Companion device ↔ backend protocol

## Authentication and transport

- HTTPS only for backend traffic outside local development.
- Device requests use `Authorization: Bearer <device_token>`.
- The device token is created by a one-time activation grant and stored in NVS. The backend stores only its SHA-256 hash.
- Yandex AI Studio and SpeechKit API keys remain server-only. They never enter HTML or firmware.
- Local device setup is a separate private-LAN HTTP portal protected by an eight-digit PIN shown on the physical screen. Never expose it to the public internet.

## Purchase → activation

1. A trusted store adapter verifies the payment provider's signature and paid status.
2. It forwards an HMAC-SHA256 signed event to `POST /api/store/webhook` using `X-Store-Signature`.
3. Backend creates a hashed, one-time grant with a 24-hour expiry.
4. Device claims it with `POST /api/devices/claim` and its MAC-derived serial.
5. Backend returns device ID and device token once. Firmware stores token and API base in NVS.

The repository provides a generic signed webhook contract. It is not connected to a specific payment provider or checkout yet.

## Speech upload

`POST /api/device/transcribe`

Headers:
`Authorization: Bearer <device_token>`
`Content-Type: application/octet-stream`

Body: mono signed 16-bit little-endian LPCM at 16 kHz, no WAV header, maximum 30 seconds and 1 MB. Backend applies device rate limits and daily audio quotas, forwards to Yandex SpeechKit and returns:

```json
{"text":"Мило, давай поиграем"}
```

Raw audio is not persisted by the application.

## Intent request

`POST /api/device/interpret`

```json
{"utterance":"Мило, давай поиграем"}
```

The backend uses device name, personality, recent context and allowlisted durable memory facts. The response contract includes:
- `addressed_to_companion`
- `understood`
- `intent`
- `action`
- `emotion`
- `confidence`
- `nonverbal_cue`
- `clarification`
- optional `memory_updates`

Firmware validates booleans, action/emotion allowlists and confidence 0..1. It applies behavior only when addressed and understood are true. Backend response is data, never executable instructions.

## Owner memory and profile

- `GET /api/device/memory` — export bounded recent context and safe preference facts.
- `DELETE /api/device/memory` — erase all device memory.
- `PATCH /api/device/profile` — update companion name and allowlisted personality traits.

These routes require the device Bearer token. The local on-device portal proxies them only after validating the physical setup PIN.

## Offline behavior

If TLS, Wi-Fi, auth, SpeechKit, AI provider, JSON parsing or schema validation fails, discard the cloud result and continue local behavior. No raw audio or API secrets are written to logs.
