# Sirokko 262.01 — комплект 0.1.7-DRAFT / BENCH ONLY

Реальный программно проверенный стендовый кандидат для ESP32-S3-DevKitC-1-N8R8 v1.1. НЕ допуск к подаче топлива/накалу/огневой эксплуатации. Плата физически не подключалась и не прошивалась. Электрические времена SPI, силовые каскады и устойчивое горение не квалифицированы.

Source main: `1e532416b1061be485981450088d2f469e6d7058`. Более поздний commit только публикует этот комплект; скомпилированный source не подменён публикационным SHA. `source/source-identity.json` содержит 135 файлов с размерами/хэшами: 72 Git-файла firmware плюс фактические sdkconfig/dependencies.lock/неизменённые managed components. Hardware/GPIO не изменялись; параллельные аппаратные commits сохранены.

## Исправление TFT, из-за которого заменён комплект

0.1.6-DRAFT отозван как итоговый: TFT 20MHz давал 50ns, тогда как ST7789V v1.3 §7.4.3/Table6 требует write cycle≥66ns. Исторический комплект сохранён без изменений, но не использовать его как новую стендовую прошивку.

В этом source единственный TFT-драйвер настроен на 10MHz/mode0, CS pre/post 1+1 cycles (номинально 100ns), общий render/DMA размер 240×20, 16 полос/кадр. Буфер 9600 bytes, пиксельный провод полосы 7.68ms, кадра 122.88ms; MAX31856 остаются mode1/5MHz с отдельными CS9/10, TFT CS47. Подробности официального ESP-IDF пути, CS-high/D-C/фронтов и обязательной осциллографии — `docs/tft_spi_timing_ru.md`. Логическая конфигурация не является физическим допуском модуля.

## Что лежит в комплекте

- `firmware/`: настоящие app/bootloader/partition-table/ota_data_initial и штатные flash_args/flasher_args.json.
- `build-config/`: полный sdkconfig + JSON, dependency lock, defaults/partitions, фиксированный Compose-рецепт, cJSON и managed components с лицензиями.
- `source/`: полный firmware source archive, SHA-256 identity, точный исходный tar для чистой target-сборки (без старых release/аппаратных файлов).
- `logs/`: свежие target/host журналы, отдельный повтор из распакованного source, esptool metadata/partition table, сверка native-input и всех 72 исходных файлов после компиляции, START+RESET probe, UI/package verification.
- `ui/`: 12 PNG 240×320 + contact sheet 1040×1020 — программная симуляция production-рендерера, не фото платы.
- `docs/`: руководства, API, аппаратный контракт и матрица источников/лицензий.
- `verification/extra-buttons.cpp`: дополнительная воспроизводимая матрица START+RESET по 10 этапам, не отдельный production-путь.
- `manifest.json`, `SHA256SUMS`: фактические размеры/хэши/конфигурация/результаты/ограничения.

Из корня комплекта: `sha256sum -c SHA256SUMS`.

Результат: 97/97 C++ + 4/4 Python; дополнительно 10/10 START+RESET сравнений. Новый display suite компилирует production LocalDisplay и ядро с подменённым SDK. В 10s serial-bus МОДЕЛИ: 501 control pass, 250 спадов heartbeat, max40ms, max MAX wait7625us при запросах во время TFT DMA. Это НЕ реальные SPI/RTOS/датчики.

Source распакован и проверен отдельно: 135/135 файлов identity, повтор 97/97 C++ + 4/4 Python, 13/13 новых PNG совпали побайтово с комплектом. Все 12 кадров просмотрены. При сравнении со старым layout 10 кадров совпали побайтово, 2 отличаются только выводом новой версии.

Target build в новом пустом каталоге: exit0. App `0x102750` / 1058640 bytes; OTA-слоты по `0x300000`, свободно по `0x1fd8b0` / 2087088 bytes. App SHA256 `14129f8ec8ef403338b2a00e7190aac7d94bd10d8312be866e59ef2dc5239c0b`. esptool подтвердил version0.1.7-DRAFT, ESP32-S3, checksum/hash valid, ESP-IDFv5.5.2. Это не чтение памяти физической платы.

## Повтор исходников и host-проверок

Работать в пустом каталоге, не поверх рабочего репозитория/PCB:

```sh
mkdir work
# Внутри source уже есть sdkconfig, dependency lock и managed_components.
tar -xzf source/sirokko-26201-controller-0.1.7-firmware-source.tar.gz -C work
mkdir work/host-deps
tar -xzf build-config/cjson-source.tar.gz -C work/host-deps
(cd work/firmware && SIROKKO_CJSON_DIR=../host-deps/cJSON bash tests/run_host_tests.sh)
(cd work/firmware && python3 tools/render_previews.py build-host/ui)
```

Нужны C++20/C toolchain, Python3, OpenSSL CLI/libcrypto development headers. Host-прогоны не устанавливают SDK. `--core-only` не является полной приёмкой.

Для target-сборки скопировать `build-config/compose.yaml` в `work/`, из `work/`:

```sh
docker compose -p sirokko-rebuild run --rm idf
```

Официальный образ `espressif/idf@sha256:05cbfc42ed2e987b8026722c15bf1d8523d3e4fd1b4ac04d2e4056f5e0918b99`, ESP-IDFv5.5.2, 2CPU/4GB/512PID, cap_drop=ALL/no-new-privileges, без портов/чужих сервисов. Внутри: `idf.py --version && idf.py --no-ccache reconfigure && cmake --build build --parallel 2`. Нужен разрешённый исходящий Component Registry; offline target build не обещается. Новое время компиляции меняет бинарный hash; побайтовая воспроизводимость пересборки не заявляется, приложенные образы проверяются SHA.

SDK: flash8MB/DIO80MHz, OctalPSRAM80MHz boot-init/memtest, rollback=true, FreeRTOS100Hz. В журнале сохранён optional DS18B20_SENSOR_HUB warning и штатный reconfigure; внешние зависимости не исправлялись.

## Первая запись и USB recovery — инструкция, не выполненное действие

При переходе со старой factory-only 0.1.5 нужна локальная USB-запись всего нового набора. Снять питание силовых нагрузок/топлива; сохранить текущую NVS в защищённое хранилище; отдельно согласовать физический порт и загрузчик. Не выполнять erase_flash автоматически. Schema1/2 не мигрируются молча; принятый профиль и run_low_qualified требуют собственных измерений, а не чужих порогов.

Из `firmware/` с официальным esptool в ESP-IDF окружении:

```sh
python -m esptool --chip esp32s3 -b 460800 \
  --before default_reset --after hard_reset write_flash @flash_args
```

Адреса из настоящей partition table: bootloader0x0, partitions0x8000, initial otadata0x10000, app/ota_0=0x20000, ota_1=0x320000. Старый app address0x10000 НЕ использовать. Не прошивать работающий отопитель. После reboot обязательны RECOVERY_PURGE, устранение аварий, квалифицированный физический RESET и новый локальный START; самопуска нет.

## Лицензии, ключи и физические границы

Espressif DS18B20 0.4.0 / onewire_bus1.1.2 — неизменённые исходники с Apache-2.0 LICENSE/SPDX; cJSON из того же SDK с MIT LICENSE. Референсы Afterburner/Webastardo/Chinook/Zorro не скопированы; URL/commits/лицензии/адаптация — `docs/reference_features_ru.md`. Лицензия своих материалов не менялась. Полный ST7789V PDF с маркировкой Sitronix Confidential не публикуется, только ссылка/краткие атрибутированные параметры.

Рабочий ключ владельца/брокер/туннель не настроены. Host RSA-3072/SHA-256 настоящий, TLS/RTOS/flash/otadata подменены. Нет аппаратной OTA/rollback/power-cut проверки, eFuse/secure boot/flash encryption не менялись. Публичные порты/инфраструктура не создавались. Непроверенные границы — `VERIFICATION_RU.md`; физический стенд без топлива обязателен, огневого допуска нет.
