# AI Companion firmware — Waveshare ESP32-S3-Touch-AMOLED-1.75C

Начальный ESP-IDF каркас под точную плату Waveshare ESP32-S3-Touch-AMOLED-1.75C. Это не готовый firmware release.

## Реализовано в исходниках

- ESP-IDF project с managed Waveshare BSP `waveshare/esp32_s3_touch_amoled_1_75c` версии `^3.0.0`.
- LVGL 9.5 и запуск дисплея через `bsp_display_start()`.
- AMOLED UI с глазами, ртом и пятью локальными состояниями.
- Автономная смена настроения и реакция на касание.
- HTTPS-клиент `POST /api/device/interpret`, который читает `api_base` и `device_token` из NVS namespace `companion`.
- TLS certificate bundle.
- Проверка JSON-ответа backend: обязательные boolean-поля, action/emotion allowlists и confidence 0..1. Неизвестный/неполный ответ отклоняется.

## Сборка

В проекте пока не зафиксирована фактическая проверенная версия toolchain. Используйте ESP-IDF и BSP-совместимую версию, сверяясь с release notes официального Waveshare managed component.

В каталоге `firmware/esp-idf`:

```bash
idf.py set-target esp32s3
idf.py build
idf.py -p COMx flash monitor
```

Сборка в текущей рабочей среде не выполнялась. Не считайте команды подтверждением совместимости до успешного CI build или локальной сборки с установленным ESP-IDF.

## Протокол устройства

```
POST /api/device/interpret
Authorization: Bearer <device_token>
Content-Type: application/json

{"utterance":"Мило, давай поиграем"}
```

Backend возвращает ограниченный `InterpretResult`. Протокол описан в [DEVICE_PROTOCOL.md](../../docs/DEVICE_PROTOCOL.md).

**Важно:** cloud client пока не подключён к распознаванию речи и циклу поведения в `main.c`. Сейчас наличие HTTP-клиента и валидатора не означает, что физическое устройство уже слушает владельца или применяет ответ ИИ.

## Пока не реализовано

- Wi-Fi provisioning, сохранение сети и автоматическое переподключение.
- Enrollment flow, который безопасно записывает device token в NVS.
- ASR с микрофонов ES7210 или другого подтверждённого решения.
- Вывод звука через ES8311 и готовая звуковая библиотека.
- Подключение cloud intent к поведению и синхронизация профиля.
- Драйвер/калибровка QMI8658 IMU.
- Управление памятью владельца, factory reset и перенос устройства.
- Web flasher, магазин, проверка оплаченного заказа и one-time enrollment grant.

## Статус проверки

Физическая плата пока не приобретена. Сборка ESP-IDF, прошивка, экран, touch, Wi-Fi, аудио, микрофоны, IMU, энергопотребление и тепловые характеристики на реальном устройстве не проверялись. Код требует аппаратной интеграции и тестирования.
