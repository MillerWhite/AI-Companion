# AI Companion

Виртуальный питомец для **Waveshare ESP32-S3-Touch-AMOLED-1.75C**. Основа продукта — питомец с потребностями, характером, настроением, памятью отношений и автономными реакциями. ИИ — необязательный дополнительный канал распознавания речи, а не интерфейс взаимодействия. Основные действия владельца: погладить, покормить, поиграть, позвать по имени, ухаживать и укладывать спать.

## Новая рабочая ветка

`feat/companion-completion-foundation`

Черновик Pull Request: https://github.com/MillerWhite/AI-Companion/pull/1

## Что добавлено в программную основу

- Двенадцать эмоций с анимированной мимикой, отдельным выражением грусти, слезами/бровями и морганием.
- Потребности, доверие, игривость, темперамент, инициативность и индивидуальные привычки с локальным сохранением.
- ES8311/ES7210 BSP audio path: мягкие процедурные невербальные звуки, VAD и захват PCM 16 kHz.
- QMI8658 motion/tilt service with filtering and event cooldowns.
- SoftAP Wi-Fi provisioning, сохранение credentials и автоматическое переподключение.
- Полный программный маршрут: microphone → SpeechKit → language model → validated intent → local emotion/action.
- Ограниченная долговременная память с безопасными категориями фактов, сроком хранения и удалением владельцем.
- Локальная страница настройки устройства с PIN с экрана, одноразовой активацией, настройкой характера и управлением памятью.
- Backend с PostgreSQL/Redis adapters, daily AI/audio caps, one-time enrollment grants и generic signed payment webhook.
- ESP Web Tools installer и CI workflow для сборки/публикации tagged firmware.

## Важные ограничения

Код прошивки ещё не прошёл подтверждённую ESP-IDF сборку или тест на физической плате. Нет live smoke test Yandex AI/SpeechKit. Веб-прошивальщик сможет загрузить образ после успешной сборки и публикации первого firmware release. Generic HMAC webhook ещё не подключён к конкретному платёжному провайдеру. Production deployment template готов, но реальный сервер, домен, секреты, backups и monitoring нужно настроить.

Подробности:
- [Статус и release gates](docs/IMPLEMENTATION_STATUS.md)
- [Протокол устройства и backend](docs/DEVICE_PROTOCOL.md)
- [Backend setup](backend/README.md)
- [Firmware setup](firmware/esp-idf/README.md)
- [Web installer](web/installer/index.html)

## Browser simulator

Откройте `AI_Companion_Emotional_Engine.html` в Chrome или Edge. Локальные эмоции и анимации работают без backend. Облачные функции из `file://` режима не работают.

## Backend local development

Из корня репозитория:

```bash
python -m pip install -r backend/requirements.txt
cp backend/.env.example backend/.env
python -m uvicorn backend.main:app --host 127.0.0.1 --port 8787
python -m unittest discover -s backend/tests -v
```

Не помещайте API keys в HTML, firmware или GitHub. Ключ, ранее раскрытый в переписке, необходимо отозвать и заменить новым серверным секретом.

## Firmware installer

Откройте `web/installer/index.html` через GitHub Pages/HTTPS. Для публикации включите GitHub Pages с источником GitHub Actions в Settings → Pages; workflow `.github/workflows/pages.yml` развернёт страницу после merge в `main` (доступность Pages для private repo зависит от плана GitHub). Используйте Chrome или Edge и USB-кабель с передачей данных. Публичный бинарник появится только после успешного workflow и тега `firmware-vX.Y.Z`.

## Hardware

Плата ещё не прошла физическую проверку. До продажи обязательны build, flash, audio, mic, touch, Wi-Fi, IMU, thermal/power and long-run tests. См. [release gates](docs/IMPLEMENTATION_STATUS.md).
