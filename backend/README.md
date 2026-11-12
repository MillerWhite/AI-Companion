# AI Companion — Backend v1.9

FastAPI service for language interpretation and device authentication. Backend does not control display, sound, or hardware directly. Device firmware remains responsible for validating and applying bounded intents.

## Configuration

Use environment variables (or local `backend/.env` for development):

- `APP_ENV=development|production`
- `AI_PROVIDER=yandex`
- `AI_API_KEY` — server-only secret
- `AI_FOLDER_ID` — Yandex Cloud folder ID
- `AI_API_BASE_URL` — configured provider base URL
- `AI_MODEL` — model alias/URI confirmed for the account
- `DEVICE_ENROLLMENT_SECRET` — server-only enrollment/admin secret
- `DEVICE_DB_PATH` — prototype SQLite database path
- `DEVICE_REQUESTS_PER_MINUTE` — per-process device request limit
- `AI_ALLOWED_ORIGINS` — exact production website origins

The repository currently contains a Yandex OpenAI-compatible request assumption. It has **not** passed a live provider smoke test. Confirm endpoint, model URI, authentication header and JSON-mode support against current Yandex docs before production. Do not assume the example model alias is available in every folder/account.

## Routes

- `GET /health` — service/provider configuration diagnostics (never returns secrets).
- `POST /api/interpret` — development-only simulator route; returns 404 in production.
- `POST /api/devices/enroll` — trusted server-side enrollment using `X-Enrollment-Secret`. Never call from public browser code.
- `DELETE /api/devices/{device_id}` — trusted server-side token revocation.
- `POST /api/device/interpret` — device Bearer token required; device name/personality are loaded server-side.

## Intent contract

Every model response must include:
`addressed_to_companion`, `understood`, `intent`, `action`, `emotion`, `confidence`, `nonverbal_cue`, `clarification`.

Actions and emotions are enums. Unknown values, missing address detection, or confidence outside 0..1 are rejected. The model is instructed not to generate speech, TTS, code, hardware commands or arbitrary sound instructions.

## Device context

For authenticated devices, backend stores a bounded rolling context of at most eight text/intent turns in SQLite. This is a prototype memory feature, not a complete user memory product. It currently has no owner-facing reset/export/delete endpoint or configurable retention policy. Do not deploy this database for sensitive personal conversations until those controls exist. Raw audio is not accepted or stored.

SQLite and in-process rate limiting are single-instance prototype choices. Production needs managed PostgreSQL, shared rate limiting (e.g. Redis), backups, retention jobs and monitoring.

## Local development

```powershell
py -m venv backend\.venv
backend\.venv\Scripts\Activate.ps1
py -m pip install -r backend\requirements.txt
Copy-Item backend/.env.example backend/.env
py -m uvicorn backend.main:app --host 127.0.0.1 --port 8787
py -m unittest discover -s backend/tests -v
```

## Security requirements before production

1. Rotate the API key previously disclosed in chat; configure the replacement only as a hosting secret.
2. Never expose `DEVICE_ENROLLMENT_SECRET` to browser or device.
3. Add one-time order-backed enrollment grants; the current shared secret is only an admin prototype.
4. Add account ownership, device transfer, token rotation and owner-initiated memory deletion.
5. Add IP/device/global limits with shared storage and budget alerts.
6. Deploy only over HTTPS and configure exact CORS origins.
7. Add a protected simulator login/session route before enabling browser AI in production.
8. Confirm provider endpoint/model/auth details with a real request and a fresh secret.
