# Sirokko 262.01 — комплект 0.1.6-DRAFT / BENCH ONLY

Это программно проверенный стендовый кандидат, НЕ разрешение топлива/накала/огневой эксплуатации. Плата ESP32-S3-DevKitC-1-N8R8 v1.1 физически не подключалась и не прошивалась. Силовые каскады и устойчивое горение не квалифицированы. Действующего ключа владельца/брокера/туннеля нет; неаутентифицированная OTA не включена.

Исходник собран из main `4c8f02580a8403fff48621e4b108686627cd75e0`. Более поздний публикационный commit добавляет этот комплект, но не меняет скомпилированный код. `source/source-identity.json` фиксирует все включённые файлы, native lock/sdkconfig и неизменённые managed components. Самоссылочного выдуманного commit нет.

## Содержимое и проверка

- `firmware/`: реальные app/bootloader/partition-table/ota_data_initial, штатные `flash_args`/`flasher_args.json`.
- `build-config/`: полный sdkconfig + JSON, dependencies.lock, defaults/partitions, официальный Compose-рецепт, cJSON и managed components с лицензиями.
- `source/`: полный firmware source archive без старых release-архивов и Git-credentials, SHA-256 каждого исходного файла.
- `logs/`: полный окончательный чистый ESP32-S3 build, host-регрессия, esptool image info, декодированная таблица разделов и дополнительная матрица START+RESET.
- `ui/`: 12 PNG 240×320 и contact sheet — программная симуляция production-рендерера, не фото платы.
- `docs/`: руководства по commissioning/кнопкам/сервису и матрица источников/лицензий.
- `verification/extra-buttons.cpp`: воспроизводимый дополнительный входной probe, не изменение production source.
- `manifest.json`, `SHA256SUMS`: фактические размеры/хэши/настройки/результаты и явные аппаратные ограничения.

Из корня комплекта: `sha256sum -c SHA256SUMS`.

Результат: 93/93 C++-группы + 4/4 Python-теста сервисной утилиты; отдельно 10/10 сравнений START+RESET по этапам. App `0x1026a0` (1 058 464 bytes), оба OTA-слота `0x300000`, свободно в каждом `0x1fd960`. Образ, checksum и встроенный SHA подтверждены штатным esptool; версия/ESP32-S3/flash указаны в `logs/app-image-info-v2.log`.

## Первая запись и USB recovery

0.1.5 имела factory-only layout. Для первого перехода на два OTA-слота нужна ЛОКАЛЬНАЯ USB-запись всего нового набора. Снять питание силовых нагрузок/топлива, сохранить имеющуюся NVS в защищённое локальное хранилище, отдельно согласовать физический порт/переход платы в загрузчик. Не выполнять erase_flash автоматически. Старые schema 1/2 не принимаются и не мигрируются молча; параметры берутся из измерений и явно принимаются токеном.

Из `firmware/`, с официальным esptool в ESP-IDF окружении:

```sh
python -m esptool --chip esp32s3 -b 460800 \
  --before default_reset --after hard_reset write_flash @flash_args
```

Фактические адреса: bootloader `0x0`, partition table `0x8000`, initial otadata `0x10000`, app/ota_0 `0x20000`. Старый app address `0x10000` НЕ использовать. Это инструкция, не выполненная здесь прошивка. Сохранение NVS не равно разрешению старого профиля. Не загружать на работающий отопитель; после загрузки обязательны RECOVERY_PURGE, устранение аварий, квалифицированный физический RESET и новый локальный START.

## Чистое повторение исходников и проверок

Работать в пустом каталоге, не поверх PCB/рабочего репозитория:

```sh
mkdir work
# Полный исходник уже включает sdkconfig, dependencies.lock и managed_components.
tar -xzf source/sirokko-26201-controller-0.1.6-firmware-source.tar.gz -C work
mkdir work/host-deps
tar -xzf build-config/cjson-source.tar.gz -C work/host-deps
(cd work/firmware && SIROKKO_CJSON_DIR=../host-deps/cJSON bash tests/run_host_tests.sh)
(cd work/firmware && python3 tools/render_previews.py build-host/ui)
```

Нужны C++20 toolchain, C compiler, Python3, OpenSSL CLI/libcrypto headers. Эта команда не устанавливает SDK на хост. Повторная host-регрессия из распакованного source archive выполнена отдельно, её журнал — `logs/host-from-package.log`.

Для настоящей target-сборки скопировать `build-config/compose.yaml` в `work/`, затем из `work/`:

```sh
docker compose -p sirokko-rebuild run --rm idf
```

Используется официальный `espressif/idf@sha256:05cbfc42ed2e987b8026722c15bf1d8523d3e4fd1b4ac04d2e4056f5e0918b99`, ESP-IDF v5.5.2, 2CPU/4GB/512PID, все capabilities сняты, no-new-privileges, без портов. Внутри: `idf.py --version && idf.py --no-ccache reconfigure && cmake --build build --parallel 2`. Нужен разрешённый исходящий доступ к официальному Component Registry; автономная сборка без сети не обещается. Новый compile time меняет бинарный hash: побайтовое совпадение новой сборки не обещается, готовая запись проверяется вложенными SHA.

SDK-конфигурация: flash 8MB/DIO/80MHz; Octal PSRAM 80MHz с boot init/memtest; rollback включён. Это конфигурация компиляции, не опрос физической памяти платы. В чистом журнале сохранены предупреждение optional `DS18B20_SENSOR_HUB` и штатный повтор конфигурации: после повтора sensor_hub пропущен. Внешние библиотеки не исправлялись.

## Сторонние компоненты и границы

Неизменённые Espressif DS18B20 0.4.0 / onewire_bus 1.1.2 поставлены с исходниками/LICENSE/SPDX; Apache-2.0. cJSON взят из того же ESP-IDF для host-теста, его MIT LICENSE сохранён в архиве. ESP-IDF/OpenSSL используются как штатные внешние инструменты. Референсы Afterburner/Webastardo/Chinook/Zorro не скопированы; commits/URL/лицензии и адаптация — `docs/reference_features_ru.md`. Лицензия собственных материалов не менялась.

Непроверенные физические границы и обязательный стенд — `VERIFICATION_RU.md`. TLS/MQTT/OTA на S3 и реальный rollback/power-cut требуют платы и будущего разрешённого доступа. SHA/подпись в host-тесте не заменяют аппаратный опыт. eFuse/secure boot/flash encryption не трогались, инфраструктура/публичные порты не создавались.
