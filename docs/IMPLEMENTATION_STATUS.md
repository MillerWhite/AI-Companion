# Статус реализации AI Companion

> **Обновлённая продуктовая концепция:** AI Companion — электронный друг, а не тамагочи. Голод, кормление и обязательный уход не являются целевыми механиками. Новая веб-консоль работает с теми командами, которые уже поддерживает USB-протокол. Карта функциональности и оставшихся работ: [PRODUCT_DIRECTION.md](PRODUCT_DIRECTION.md).

# AI Companion — implementation status 2.3

This is a code-level status, not a claim of hardware or production acceptance. A feature is not release-ready until its CI and physical acceptance checks pass.

## Product invariants

- Nonverbal companion only: no TTS and no spoken assistant responses.
- Local firmware owns animation, emotion state, touch, sensor reactions and offline behavior.
- Cloud model returns constrained intent data; firmware validates before applying it.
- API keys stay on the backend.
- Audio is uploaded only for transcription, is not persisted by this application, and is subject to daily quotas.
- Memory is bounded, owner-visible and owner-deletable.

## Requested work items

| Task | Code status | Remaining acceptance gate |
|---|---|---|
| Full emotional transitions and facial expressions | Implemented in firmware/browser code: thirteen mood states, animated eye/mouth geometry, sadness brows/tears, blink and event-driven transitions | ESP-IDF build; physical display smoothness and emotional clarity test |
| Unique personality and long-term development | Implemented foundation: temperament, initiative, selectable quirks, trust/curiosity/playfulness, persistent local state and safe durable preference facts | Broader traits/quirks, multi-month progression tuning and owner data export review |
| Physical speaker sound system | Implemented driver path: ES8311 BSP handle, soft procedural motifs, low gain and cooldown | Board test, audio calibration, echo/feedback and listening-quality review |
| Microphone speech recognition | Implemented pipeline: ES7210 BSP capture, VAD, mono 16 kHz PCM, backend OpenAI audio transcription route | Board microphone slot mapping, Russian/noise tests, live OpenAI audio transcription credentials and quota test |
| Wi-Fi and automatic reconnect | Implemented: SoftAP Security 1 provisioning, NVS credential persistence, exponential reconnect | First-boot test with actual router and recovery/reset test |
| Cloud intent → firmware behavior | Implemented: voice → OpenAI audio transcription → intent → allowlist validation → emotion/action application | Live OpenAI model smoke test and real device integration test |
| Sensor-driven pet reactions | Added QMI8658 shake/dizzy and face-down blank/wake; ES7210 mic blow/clap classifiers; AXP2101 VBUS/charge/battery telemetry with contextual charging response | Threshold calibration, AMOLED blank-state power measurement, AXP2101 read validation and physical acceptance test |
| ES8311, ES7210, QMI8658, AXP2101 drivers | BSP codec handles, QMI8658 managed driver and AXP2101 register reads are wired into service modules | Verify actual BSP component resolution, build and physical register/data test |
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
2. Validate OpenAI API endpoint/model/auth against current account docs using a new key. Revoke any key previously disclosed in project chat.
3. Validate OpenAI audio transcription account permissions, folder scope, Russian recognition quality and actual quotas.
4. Purchase and test the exact Waveshare ESP32-S3-Touch-AMOLED-1.75C board.
5. Verify display, touch, codec sample rate/channel routing, VAD threshold, sound level, QMI sensor and Wi-Fi provisioning on hardware.
6. Connect a real payment provider adapter; the generic HMAC event is not a checkout integration.
7. Deploy to a real host with DNS, managed secrets, database backups, restore drill, monitoring and provider spend alerts.
8. Publish a tagged firmware release; until then the web installer manifest has no downloadable release binary.

## Explicitly not claimed

- No successful firmware build or physical board test has been observed yet. Sensor reaction thresholds and BLE pairing remain code-level implementations until calibrated and accepted on the exact board and enclosure.
- No public production backend has been deployed.
- No actual payment provider has been connected.
- No OpenAI or OpenAI audio transcription live request has been verified with a rotated secret.
- No production monitoring, backups or restore test is configured by this repository alone.


## Обновление 12.11.2026 — одностраничная панель и новая модель компаньона

- Веб-панель перестроена в один вертикальный экран по UX-паттерну Pip-KeyPet: подключение и состояние сверху, далее имя/характер, бейдж, Quick QR, экран, действия и диагностика. Боковые вкладки и дублирующиеся карточки удалены.
- Quick QR: USB-конфигурация продолжает сохранять до четырёх ссылок в NVS. Это подтверждает хранение, но не рендер QR на AMOLED и не навигацию между QR-экранами.
- Модель состояния прошивки заменяется на событийную: в новой структуре нет hunger/comfort/social/energy; течение времени само по себе не ухудшает настроение. Взаимодействия влияют на доверие, любопытство и игривость. Удалены USB-команды feed/clean и связанные ветки обработки в main.
- Совместимость старого NVS blob: при несовпадении размера структуры существующее состояние эмоций инициализируется заново; пользовательские настройки профиля и QR хранятся отдельно и не должны затрагиваться.
- QR-код на AMOLED и свайп-навигация между четырьмя слотами реализованы в `firmware/esp-idf/main/main.c` на уровне исходного кода. До сборки и аппаратной проверки нельзя подтверждать читаемость QR, работу жестов и устойчивость рендеринга на дисплее.
- Не завершено и не заявляется как готовое: физическая приёмка BLE ANCS/AMS для iPhone и BLE HID-пульта; подтверждение поведения на нескольких моделях iOS и компьютерах.
- Проверка физического устройства невозможна до получения платы. Сборка ESP-IDF и GitHub Actions после этих изменений ещё должны завершиться успешно; до этого новая прошивка не считается проверенной.


## Обновление 12.11.2026 — двусторонние события USB

- USB Serial теперь передаёт отдельные записи `@M` (текущая эмоция) и `@T` (физическое событие) от прошивки к панели.
- Веб-панель запрашивает текущее настроение при подключении и обновляет выражение лица по эмоциям прошивки, а не только по локальному нажатию в браузере.
- Прошивка сообщает о касании экрана, встряхивании, положении экраном вниз/вверх, хлопке, потоке воздуха и подключении питания.
- Запись USB сериализована mutex-защитой, чтобы события из задач датчиков не смешивались с ответами конфигурации и диагностики.
- Команда `play` удалена из USB allowlist; стандартный Bluetooth HID play/pause для управления компьютером/медиа не затрагивается.
- Проверка исходников не заменяет сборку ESP-IDF и тест на физической плате.


## Обновление 12.11.2026 — упрощение панели и консолидация эмоциональной системы

- Удалено из главного описания панели упоминание механик голода, кормления и обязательного ухода.
- Основные настройки оставлены на виду; инструкции по компьютеру, iPhone и диагностике свернуты в компактные раскрывающиеся блоки.
- Удалены повторяющиеся пояснения и пустой раздел обновлений, который пока не выполнял действий.
- AI_Companion_Emotional_Engine.html пока сохраняется как резервный прототип для сверки. Основные алгоритмы характера, отношений, инициативы и эмоций уже перенесены в firmware/esp-idf/main/companion_emotion.c; удаление файла отложено до успешного CI и финальной проверки полноты переноса.
- Основной источник аппаратного состояния и эмоций — firmware/esp-idf/main/companion_emotion.c; веб-панель отображает события прошивки. Следующий gate — CI, после чего остаётся сверить недостающие визуальные/звуковые детали прототипа.
- Исправлен повторный прирост curiosity при одном событии pet: теперь одно касание даёт один прирост.
- Эти правки не подтверждают ESP-IDF build или работу на физической плате.


## Обновление 12.11.2026 — перенос эмоционального ядра и CI

- Логика характера и отношений перенесена в основное ядро `companion_emotion.c`: доверие, любопытство, игривость, спокойствие, отзывчивость и счётчики событий.
- Инициативность теперь рассчитывается общим алгоритмом с учётом выбранного темперамента и накопленной отзывчивости. Время само по себе не ухудшает отношения и не создаёт потребности в уходе.
- Касание, длинное касание, зов, встряхивание, отдых, пробуждение, удивление и подключение зарядки учитываются как события отношений. Состояние хранится в NVS.
- Backend получил серверный фильтр чувствительных значений памяти и авторизованный JSON-экспорт по `GET /api/device/memory/export`.
- Добавлены native C tests для эмоционального ядра и Python contract tests для backend.
- Добавлен quality workflow: backend tests, host-side C tests и проверка синтаксиса JavaScript основной панели. Firmware workflow запускается только для `main` и тегов релиза; pull request и feature-ветки из его триггеров удалены.
- Основная панель получила ESP Web Tools installer, использующий `web/installer/manifest.json` и последний опубликованный firmware release.
- Отдельный `AI_Companion_Emotional_Engine.html` пока не удалён: окончательное удаление разрешено только после успешного CI и проверки, что все нужные реакции/анимации и настройки действительно доступны в основной прошивке. Прототип пока оставлен как резерв для сверки.
- Новая структура `companion_state_t` меняет размер NVS blob. При первом запуске новой прошивки старый blob будет инициализирован заново; профиль, имя, QR и настройки экрана хранятся отдельно.
- CI ещё должен фактически завершиться успешно. Ни один результат host-side тестов не заменяет ESP-IDF build, прошивку устройства или физическую приёмку.

## Обновление 12.11.2026 — подготовка pre-hardware CI

- Host-side CI объединён в один workflow `quality.yml`: backend contract tests, native emotion tests, procedural voice tests, motion classifier tests и проверка JavaScript установщика.
- Добавлена проверка соответствия версии `web/installer/manifest.json` файлу `firmware/esp-idf/version.txt`, целевого чипа ESP32-S3 и ожидаемого имени firmware release asset.
- Удалён дублирующий workflow `backend-tests.yml`, который повторял backend и native C проверки и дополнительно запускался на PR/feature-ветках.
- Отдельные workflow сборки ESP-IDF и публикации сайта сохранены.
- CI после изменений должен завершиться успешно; queued workflow runs не считаются пройденными проверками.
- В репозитории пока нет опубликованного GitHub Release с прошивкой. Ссылка установщика на `releases/latest/download/AI-Companion-ESP32-S3-Touch-AMOLED-1.75C.bin` заработает только после успешной сборки и публикации релиза с бинарником и SHA256.
