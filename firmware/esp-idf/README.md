# AI Companion firmware — Waveshare ESP32-S3-Touch-AMOLED-1.75C

Первый firmware-каркас под точную плату Waveshare ESP32-S3-Touch-AMOLED-1.75C.

## Реализовано в исходниках

- ESP-IDF project с официальным managed BSP waveshare/esp32_s3_touch_amoled_1_75c версии ^3.0.0.
- LVGL 9.5 и запуск дисплея через официальный bsp_display_start().
- Чёрный AMOLED-интерфейс с глазами, ртом и пятью локальными состояниями.
- Автономная смена настроения и реакция на касание лица.
- HTTPS-клиент POST /api/device/interpret. Он читает api_base и device_token из NVS namespace companion, отправляет Bearer token и проверяет TLS через certificate bundle. Ключ Yandex AI на плате не хранится.

## Сборка

Нужен ESP-IDF v5.5.5 или v6.0.2 и установленный ESP-IDF Component Manager. В каталоге firmware/esp-idf выполните:

- idf.py set-target esp32s3
- idf.py build
- idf.py -p COMx flash monitor

Компоненты загрузятся при конфигурации проекта.

## Протокол устройства

POST /api/device/interpret
Authorization: Bearer <device_token>
Content-Type: application/json

Тело: {"utterance":"..."}

Ответ — структурированный InterpretResult backend. Перед вызовом cloud client Wi-Fi должен быть подключён. Ключи NVS api_base и device_token пока должны быть записаны отдельным provisioning-процессом.

## Пока не реализовано

- Wi-Fi provisioning и ввод сети владельцем.
- Запись device token в NVS из enrollment-flow.
- Подключение ES8311/ES7210 и финальные звуковые паттерны.
- ASR на плате.
- Применение облачного JSON-ответа к локальной машине состояний.
- Веб-флешер, магазин и проверка оплаченного заказа.

## Статус проверки

Исходники подготовлены на основе официального ESP-IDF примера Waveshare 02_lvgl_demo_v9 и официального BSP. Сборка ESP-IDF и запуск на физической плате ещё не выполнялись: физической платы пока нет. Это исходный каркас, не готовый бинарный релиз.
