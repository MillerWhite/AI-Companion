# AI Companion backend 2.1

FastAPI service for natural-language intent interpretation, SpeechKit transcription, device enrollment and owner controls. The backend returns constrained data; it never directly drives hardware.

## Implemented routes

| Route | Purpose | Authorization |
|---|---|---|
| `GET /health` | Health and configuration status | Public |
| `POST /api/interpret` | Browser simulator development route | Disabled in production |
| `POST /api/devices/claim` | Consume a one-time enrollment grant | Grant bearer in body |
| `POST /api/device/transcribe` | 16 kHz, mono, 16-bit LPCM → Yandex SpeechKit | Device Bearer token |
| `POST /api/device/interpret` | Speech text → validated nonverbal intent | Device Bearer token |
| `GET /api/device/memory` | Export bounded memory JSON | Device Bearer token |
| `DELETE /api/device/memory` | Delete all saved context and facts | Device Bearer token |
| `PATCH /api/device/profile` | Update companion name/personality | Device Bearer token |
| `POST /api/admin/enrollment-grants` | Create one-time activation grant | Server-only enrollment secret |
| `POST /api/store/webhook` | HMAC-signed paid-order event → one-time grant | Store webhook signature |
| `POST /api/devices/enroll` | Legacy trusted admin enrollment | Server-only enrollment secret |
| `DELETE /api/devices/{device_id}` | Revoke device | Server-only enrollment secret |

## Speech pipeline

Firmware sends mono signed 16-bit LPCM at 16 kHz over HTTPS. The backend limits request size/duration, applies per-device daily audio quotas, and forwards audio to the synchronous Yandex SpeechKit recognition endpoint. Raw audio is not stored. Transcribed text is passed separately to the language model. Unaddressed ambient speech is not retained in the device conversation context; only speech the model identifies as addressed to the companion is saved.

Configure a dedicated `SPEECHKIT_API_KEY` with the SpeechKit STT role and `SPEECHKIT_SAMPLE_RATE=16000`. For a service-account API key, leave `SPEECHKIT_FOLDER_ID` empty (the folder is derived from the service account); set it only when authenticating as a user account. Confirm current account quotas before enabling the device microphone.

## Memory

Each device stores:
- At most eight recent owner/companion context turns.
- Up to twenty non-sensitive preference facts in four allowlisted categories.
- A retention window controlled by `MEMORY_RETENTION_DAYS` (default 90 days).

Memory writes require the model to mark the utterance as addressed to the companion and return an allowlisted memory category with confidence >= 0.85. The prompt forbids sensitive information, credentials and inferred personal facts. Owners can export or delete memory using the authenticated routes; the on-device local owner portal proxies these actions after its physical setup PIN is supplied.

This is a bounded companion memory feature, not a general-purpose personal data store. Review the categories and retention policy before commercial release.

## Enrollment and payments

- Admins can issue a short-lived, single-use grant after independently verifying a paid order.
- `POST /api/store/webhook` accepts a generic JSON event signed with HMAC-SHA256 using `STORE_WEBHOOK_SECRET`.
- The grant is stored hashed and can be redeemed once by a device serial.
- The device token is returned once and stored in NVS by the firmware.

**A real payment provider is not connected yet.** The webhook is a provider-neutral adapter contract; the store must verify the provider's signature/payment status and forward a signed event. Do not trust arbitrary client-side `payment_status=paid` claims. Customer accounts, order history, refunds and automated revocation on chargeback are not implemented.

## Production controls

- PostgreSQL support via `DATABASE_URL`; SQLite is for local development.
- Redis-backed per-device rate limits and daily AI/audio request budgets.
- Production startup refuses to run without PostgreSQL, Redis, strong enrollment/webhook secrets, AI and SpeechKit keys, exact CORS origins and allowed hosts.
- Docker Compose template includes PostgreSQL, Redis and Caddy automatic HTTPS.
- API responses include security headers; API responses are not cached.
- Audio request bodies and store webhooks are size-limited before buffering.

The daily AI limit is a request-count ceiling, not a provider-billing meter. Add provider spend alerts, token accounting, database backups, restore drills, monitoring and incident alerts before production.

## Local development

```bash
python -m venv .venv
# Windows: .venv\\Scripts\\activate
# macOS/Linux: source .venv/bin/activate
python -m pip install -r backend/requirements.txt
cp backend/.env.example backend/.env
python -m uvicorn backend.main:app --host 127.0.0.1 --port 8787
python -m unittest discover -s backend/tests -v
```

## Production template

From the `backend` directory on a Linux host with a DNS A/AAAA record pointing to it:

```bash
cp .env.production.example .env
# Fill all secrets, API_DOMAIN, exact origins and database credentials.
docker compose -f docker-compose.production.yml up -d --build
```

Caddy obtains TLS certificates for `API_DOMAIN`. Keep `.env` out of Git and restrict its permissions. Put provider keys in a managed secret store where available. This repository contains deployment templates; no cloud host has been provisioned or deployed by this change.

After deployment, schedule `bash scripts/backup-postgres.sh` daily from the `backend` directory. It creates a checksum-protected local dump and removes local dumps older than 14 days. Copy backups off-host and perform restore drills; a local dump on the same server is not a disaster-recovery plan.

## Provider caveat

The Yandex OpenAI-compatible chat endpoint/model/auth header configuration still needs a live smoke test using a newly created, unexposed key. The key disclosed earlier in the project conversation must be revoked and must not be reused.
