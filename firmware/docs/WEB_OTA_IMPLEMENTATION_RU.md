# Web-интерфейс и подписанная OTA Sirokko

## Назначение

Модуль `firmware/main/web.cpp` реализует автономный русскоязычный интерфейс ESP-IDF HTTP Server для AP Sirokko. Он не управляет GPIO напрямую: все действия передаются в общий runtime/автомат.

Маршруты:

- `GET /` — автономный Svelte HTML/CSS/JS/шрифты без CDN, gzip-ресурс `main/web_ui_data.inc`;
- `POST /api/login` — пароль AP в заголовке `X-Sirokko-Password`, затем cookie `HttpOnly; SameSite=Strict`; серверный session TTL — 15 минут;
- `GET /api/state` — русское состояние, EXH/AIR/room, outputs, timer/arm/purge, range/calibration, process boundary, storage и event journal;
- `POST /api/challenge` — одноразовый challenge для всех действий кроме STOP;
- `POST /api/command` — `START`, `RESET_FAULT`, `SETTINGS`, `CALIBRATE`, `TIMER`, `PROCESS_CONFIG` и отдельный `STOP`;
- `POST /api/ota` — потоковая загрузка подписанного `.sota`.

UI не сообщает «применено» после `QUEUED`: оно ждёт следующего state readback. Dirty input не перезаписывается. При stale/offline отключены все auto-action controls, а `STOP` остаётся отдельным доступным маршрутом.

`qualified` в интерфейсе означает только настроенную форму профиля. Сервисный экран явно показывает `physical_tests=false`; физических примеров параметров процесса в UI нет.

## Протокол управления

`web_protocol.*` использует строгий JSON: неизвестные или дублированные поля, неверные типы, неполные payload и некорректные числа отклоняются. Фактическая длина HTTP body сохраняется до parser; embedded NUL отклоняется, валидный префикс вместо полного запроса не принимается.

Для non-STOP последовательность такова:

1. Клиент отправляет полный semantic payload на `/api/challenge` без `grant`.
2. `web_protocol` вычисляет canonical SHA-256 полного payload и runtime выдаёт grant.
3. Web связывает action + token + state version + expiry + 256-bit payload digest.
4. Клиент сверяет `state_version` и `boot_id` challenge с исходным снимком редактирования. При конфликте запрос не отправляет. Затем отправляет тот же payload и grant на `/api/command` (или метаданные upload в заголовках `/api/ota`).
5. До runtime вызова проверяются одноразовость, expiry, state-version и replay cache. Для `PROCESS_CONFIG` runtime hash — `process_fingerprint(profile)`; web SHA-256 всё равно связывает полный payload.

`STOP` авторизован cookie, но намеренно не требует challenge/подтверждения. Он проходит отдельный strict parser и idempotent replay cache, после чего вызывает только `runtime_stop(false)`.

## OTA-пакет

Принимается только один подписанный `.sota`, а не исходный `.bin`.

Wire format (`ota_verify.*`, big-endian):

| Байты | Поле |
|---:|---|
| 0..7 | `SIROTA01` |
| 8..11 | format version `1` |
| 12..15 | header size `100` |
| 16..19 | image size |
| 20..51 | project name, NUL padded |
| 52..67 | target, NUL padded |
| 68..99 | SHA-256 image |
| далее | ESP-IDF app image |
| последние 384 | RSA-3072/SHA-256 signature of the first 100 bytes |

OTA handler сохраняет transport deadline через `receive_exact_bounded`: JSON — 3 s, package — 120 s. Он разрешает поток только после runtime maintenance guard, пишет только inactive partition и на любом error вызывает abort + exit maintenance без выбора boot partition.

До `esp_ota_set_boot_partition()` проверяются:

- size/truncation и реальная inactive partition capacity;
- manifest magic/version/project=`sirokko_26201_controller`/target=`esp32s3`;
- SHA-256 всего upload, SHA-256 image и RSA-3072 signature публичным `ota_public_key.pem`;
- фактический ESP image header (`magic`, `chip_id == ESP_CHIP_ID_ESP32S3`);
- `esp_ota_end()` image validation;
- `esp_app_desc_t::project_name` записанного image;
- исходный maintenance epoch и текущие показания/выходы в задаче управления непосредственно при активации.

HTTP handler не выбирает boot partition и не перезагружает MCU. После проверки он ставит запрос `VERIFIED_ACTIVATION_QUEUED` и возвращает `expected_version` из проверенного образа. Браузер сохраняет ожидание через повторную авторизацию после reboot; совпадение новой boot identity и ожидаемой версии — наблюдение запущенной версии, не физическое подтверждение rollback. Через 120 с без подтверждения результат неизвестен. Контроллер выдерживает 200 мс для ответа браузеру без блокирующего ожидания, повторно проверяет свежие холодные термопары, отсутствие fault, выключенные выходы и действующий epoch. STOP, выход из maintenance, pending persistence или потеря свежих/холодных данных отзывают epoch и запрос. Только владелец автомата вызывает финальную `esp_ota_set_boot_partition()` и перезагрузку; эта запись flash выполняется исключительно в холодном состоянии с выключенными силовыми выходами. Её физическая длительность и поведение при сбое питания требуют стенда; обычный safety loop не ждёт network/NVS.

Для HTTP AP встроен MIT `js-sha256` 0.11.1: `crypto.subtle` доступен не на всех незащищённых HTTP-origin, поэтому браузер использует автономный SHA-256 fallback. CDN и Интернет не нужны. Источник, SHA-256 и лицензия — `firmware/third_party/`.

Rollback health confirmation не дублируется в web: после boot его выполняет runtime родителя через `esp_ota_mark_app_valid_cancel_rollback()`.

### Offline signing

Приватный RSA-3072 key находится вне Git и не передаётся в аргументах команды. Подписать собранный ESP-IDF image:

```bash
python3 firmware/tools/sign_ota.py \
  --input firmware/build/sirokko_26201_controller.bin \
  --output /safe/local/sirokko-ota.sota
```

`sign_ota.py` использует защищённый key по умолчанию либо `SIROKKO_OTA_SIGNING_KEY`, проверяет owner-only permissions и не печатает ключ. В репозитории хранится только `firmware/main/ota_public_key.pem`.

## Интеграция

Исходники уже включены в `firmware/main/CMakeLists.txt`, публичный ключ — через `EMBED_TXTFILES`. `firmware/sdkconfig.defaults` содержит `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y`; сохранены `ota_0`, `ota_1`, `otadata`. Secure Boot, eFuse anti-rollback и Flash Encryption этой работой не включались.

## Проверки

Полный изолированный host suite:

```bash
python3 firmware/tests/test_web_ota_host.py
```

Он компилирует с `-Werror` реальный `web_protocol.cpp`, `ota_verify.cpp`, `bounded_receive.cpp` и production `web.cpp` (с test-only ESP transport/runtime shim), подписывает test packages офлайн и проверяет:

- strict parser, full-payload digest, age/state/replay и STOP;
- RSA-3072 signature, altered image, project/target mismatch, oversize и truncation;
- absolute receive deadline and no sink application after deadline;
- actual HTTP handlers: login/auth/state/challenge/command/STOP/OTA success and wrong actual image chip abort without boot activation;
- production frontend script: Russian state/event render, dirty fields, stale lockout and separate STOP;
- embedded NUL в challenge/command: отказ без выдачи grant/постановки префикса;
- потерю maintenance непосредственно внутри `esp_ota_end()`: нет активации/перезагрузки;
- native/fallback SHA-256, включая 1 MiB binary vector, без `crypto.subtle`.

`test_ota_activation.cpp` дополнительно проверяет отзыв lease, старый epoch после повторного входа, задержку активации и свежесть/температуру/выходы. Он включён в общий `run_host_tests.py`.

Дополнительно отдельный scratch-only Docker probe в закреплённом ESP-IDF 5.2.3 image собрал и слинковал фактические `web.cpp`, `web_protocol.cpp`, `ota_verify.cpp` и `bounded_receive.cpp` для `esp32s3` с `-Werror` и embedded public PEM. Он не меняет CMake проекта, но использует минимальные runtime stubs и factory partition — это compile/link proof, а не общая project-сборка.

Chromium/WebKit получают точный gzip/CSP из production handler через loopback fixture server. Проверяются синтетические гонки и действительный сериализованный host JSON. Дополнительно SHA fallback выполняется на небезопасном HTTP origin, не localhost. Это программные проверки, не физическое испытание AP/ESP/flash. Подробности: `firmware/web_ui/README_RU.md`.

## Непроверенное этим модулем

Полная интегрированная сборка ESP-IDF 5.2.3 для ESP32-S3 после исправлений завершена успешно. Не выполнялись физическая прошивка, reboot в OTA slot, pending-verify rollback health и физические проверки горения; это отдельные стендовые сценарии.
