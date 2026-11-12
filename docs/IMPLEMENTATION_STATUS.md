# AI Companion — implementation status 2.1

This is a code-level status, not a claim of hardware or production acceptance. A feature is not release-ready until its CI and physical acceptance checks pass.

## Product invariants

- Nonverbal companion only: no TTS and no spoken assistant responses.
- Local firmware owns animation, needs, touch, sensor reactions and offline behavior.
- Cloud model returns constrained intent data; firmware validates before applying it.
- API keys stay on the backend.
- Audio is uploaded only for transcription, is not persisted by this application, and is subject to daily quotas.
- Memory is bounded, owner-visible and owner-deletable.

## Requested work items

| Task | Code status | Remaining acceptance gate |
|---|---|---|
| Full emotional transitions and facial expressions | Implemented in firmware/browser code: ten moods, animated eye/mouth geometry, sadness brows/tears, blink and need-driven transitions | ESP-IDF build; physical display smoothness and emotional clarity test |
| Unique personality and long-term development | Implemented foundation: temperament, initiative, selectable quirks, needs, trust/playfulness, persistent local state and safe durable preference facts | Broader traits/quirks, multi-month progression tuning and owner data export review |
| Physical speaker sound system | Implemented driver path: ES8311 BSP handle, soft procedural motifs, low gain and cooldown | Board test, audio calibration, echo/feedback and listening-quality review |
| Microphone speech recognition | Implemented pipeline: ES7210 BSP capture, VAD, mono 16 kHz PCM, backend SpeechKit route | Board microphone slot mapping, Russian/noise tests, live SpeechKit credentials and quota test |
| Wi-Fi and automatic reconnect | Implemented: SoftAP Security 1 provisioning, NVS credential persistence, exponential reconnect | First-boot test with actual router and recovery/reset test |
| Cloud intent → firmware behavior | Implemented: voice → SpeechKit → intent → allowlist validation → emotion/action application | Live Yandex model smoke test and real device integration test |
| ES8311, ES7210, QMI8658 drivers | BSP codec handles and QMI8658 managed driver are wired into service modules | Verify actual BSP component resolution, build and physical register/data test |
| Owner server-memory controls | Implemented: bounded context, safe fact categories, 90-day expiry, export/delete/profile APIs and PIN-protected local portal | Privacy review, retention job/backup review and owner usability test |
| Automatic website firmware flashing | Implemented: ESP Web Tools page, manifest and tagged release workflow | Successful firmware CI build and published release asset; browser/USB test |
| Registration after purchase | Implemented generic flow: HMAC-signed paid event → hashed single-use grant → serial claim → NVS token | Connect and verify an actual payment provider, refunds/chargebacks and order/account system |
| Production backend and cost protection | Implemented deployment foundation: PostgreSQL, Redis, daily AI/audio caps, Caddy TLS, production config checks | Real host/DNS/secrets, backup/restore, spend alerts, monitoring, load/security tests and deployment |

## Backend safeguards

- SQLite development mode; PostgreSQL production mode.
- Redis shared device rate limit, daily model request cap, per-device audio cap and global daily audio cap.
- Body size limits before buffering audio and payment events.
- Server-only secrets and exact production CORS/host allowlists.
- Device token hashes at rest; one-time activation grants hashed at rest.
- Memory context limited to eight turns; up to twenty non-sensitive facts from four allowlisted categories; default retention 90 days with scheduled database cleanup.
- Memory export/reset and profile update require device authentication.
- Production startup refuses to run without PostgreSQL, Redis, server secrets, provider keys, CORS and host configuration.

## Release blockers

1. Run GitHub Actions backend tests and ESP-IDF build; fix all failures.
2. Validate Yandex AI Studio endpoint/model/auth against current account docs using a new key. Revoke any key previously disclosed in project chat.
3. Validate SpeechKit account permissions, folder scope, Russian recognition quality and actual quotas.
4. Purchase and test the exact Waveshare ESP32-S3-Touch-AMOLED-1.75C board.
5. Verify display, touch, codec sample rate/channel routing, VAD threshold, sound level, QMI sensor and Wi-Fi provisioning on hardware.
6. Connect a real payment provider adapter; the generic HMAC event is not a checkout integration.
7. Deploy to a real host with DNS, managed secrets, database backups, restore drill, monitoring and provider spend alerts.
8. Publish a tagged firmware release; until then the web installer manifest has no downloadable release binary.

## Explicitly not claimed

- No successful firmware build or physical board test has been observed yet.
- No public production backend has been deployed.
- No actual payment provider has been connected.
- No Yandex AI or SpeechKit live request has been verified with a rotated secret.
- No production monitoring, backups or restore test is configured by this repository alone.
