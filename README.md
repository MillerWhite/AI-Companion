# AI Companion

Невербальный виртуальный компаньон для **Waveshare ESP32-S3-Touch-AMOLED-1.75C**. Проект разделяет локальное поведение устройства и облачную интерпретацию естественной речи. Компаньон не разговаривает и не использует TTS.

## Текущее состояние

Рабочая ветка завершения основы: `feat/companion-completion-foundation`.

### Что есть в репозитории
- Браузерный симулятор с лицом, анимациями, эмоциями, потребностями, инициативой и локальной памятью.
- Голосовое управление браузерного прототипа через Web Speech API (зависит от браузера и сети).
- FastAPI backend с ограниченным JSON-контрактом действий и эмоций.
- Регистрация устройств с уникальным Bearer token; хранятся только хеши токенов.
- Ограниченный контекст последних текстовых обращений на сервере для физических устройств.
- ESP-IDF каркас для Waveshare и HTTPS-клиент с проверкой allowlist полей cloud response.
- Unit tests backend и GitHub Actions workflow.
- Документы статуса реализации и протокола устройства.

### Что НЕ готово
- Реальное подключение облачного JSON к циклу поведения прошивки.
- Wi-Fi provisioning, ASR на устройстве, драйверы микрофонов/динамика, звуковые паттерны на плате.
- QMI8658 IMU integration, калибровка и фильтрация.
- UI сброса/экспорта/удаления памяти.
- Веб-прошивальщик, заказ/оплата, одноразовые enrollment grants и привязка к владельцу.
- Production hosting, PostgreSQL, Redis, backups, мониторинг и бюджетные ограничения.
- Успешный live smoke test Yandex AI Studio.
- Сборка ESP-IDF и тест на физической плате.

См. [матрицу готовности и этапы](docs/IMPLEMENTATION_STATUS.md) и [протокол устройства](docs/DEVICE_PROTOCOL.md).

## Запуск браузерного симулятора

Откройте `AI_Companion_Emotional_Engine.html` напрямую в Chrome или Edge. Локальные эмоции и анимации не требуют backend. Облачный интеллект из локального `file://` режима не работает.

Для локальной разработки backend из корня репозитория:

```powershell
py -m pip install -r backend\requirements.txt
Copy-Item backend\.env.example backend\.env
py -m uvicorn backend.main:app --host 127.0.0.1 --port 8787
```

Проверка: `http://127.0.0.1:8787/health`.

Симулятор использует development-only `POST /api/interpret`. В production этот неавторизованный маршрут намеренно выключен; нельзя просто включать его на публичном сервере. Для production нужен отдельный защищённый simulator session flow или авторизованный аккаунт.

## Секреты

- Не помещайте Yandex API key в HTML, JavaScript, firmware или GitHub.
- Не используйте ранее раскрытый в переписке ключ: отзовите его и создайте новый.
- Указывайте новый ключ только в секретах backend-хостинга или локальном `backend/.env`.
- `AI_FOLDER_ID` — идентификатор каталога, не секрет.
- `DEVICE_ENROLLMENT_SECRET` — серверный секрет, никогда не передавать в браузер/прошивку.

## Backend tests

```powershell
py -m pip install -r backend\requirements.txt
py -m unittest discover -s backend/tests -v
```

GitHub Actions запускает тот же набор тестов на push и pull request.

## Hardware target

Waveshare ESP32-S3-Touch-AMOLED-1.75C: CO5300 AMOLED 466×466, CST9217 touch, QMI8658 IMU, AXP2101 PMU, ES8311/ES7210 audio. Firmware исходники находятся в `firmware/esp-idf/`.

**Важно:** плата пока не приобретена. Сборка, прошивка, звук, микрофоны, IMU, энергопотребление и тепловые характеристики не проверялись на реальном устройстве. Код не является готовым бинарным релизом.

## Стоимость AI

Не существует гарантированного безлимитного бесплатного публичного облачного API. Проверьте актуальные тарифы и квоты Yandex Cloud. В production обязательны лимиты по устройствам и бюджету.

## Официальные ресурсы

- [Waveshare docs](https://docs.waveshare.com/ESP32-S3-Touch-AMOLED-1.75C)
- [Waveshare BSP](https://github.com/waveshareteam/ESP32-S3-Touch-AMOLED-1.75C)
- [Yandex AI Studio](https://aistudio.yandex.ru/)
