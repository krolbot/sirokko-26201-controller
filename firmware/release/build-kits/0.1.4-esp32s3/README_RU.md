# Sirokko 262.01 — комплект прошивки 0.1.4-DRAFT

## Статус

Это стендовый комплект `DRAFT / BENCH ONLY` для `ESP32-S3-DevKitC-1-N8R8 v1.1`. Он собран и проверен, но не разрешает подключение топлива, свечи накала или огневой запуск. Физическая плата к этой сборке не подключалась и не прошивалась.

Основная PCB включает силовые каналы, но ток нагрузки не проходит через ESP32. Рабочее питание контроллера — от основной PCB, получающей только 24 В от внешнего БП; USB предназначен для сервиса и прошивки. Дополнительные TPS3808/brownout/reset-тракты в этот комплект не вводились. LTC7001 bootstrap не квалифицирован для разрешённых прошивкой диапазонов `1…78125 Hz` и `1…1000 permille`; динамический PWM не считается безопасным до аппаратных измерений и приёмки параметров.

## Что проверено

- исходный архив 0.1.4-DRAFT: commit `1dd9816e70979c9a0f0dd0530355eebcb8ea628a`, SHA-256 `e2d2c2774ca567e1b432f6a248a42ea490afe05ddcab0812b90cf593ea910bd8`;
- host-тесты: `39/39 passed`, exit `0`;
- чистая целевая сборка: exit `0`, официальный `espressif/idf:v5.5.2`, digest `sha256:05cbfc42ed2e987b8026722c15bf1d8523d3e4fd1b4ac04d2e4056f5e0918b99`;
- цель SDK: `esp32s3`;
- flash: `8MB`, `DIO`, `80 MHz`;
- PSRAM: включена, `Octal`, `80 MHz`, инициализация при загрузке;
- разделы: bootloader `0x0`, partition table `0x8000`, приложение `0x10000`;
- app image: 344832 байта, checksum `6b` valid, validation hash `b7905c597373c6a03bfb93f45049efc97922f0697ce808592b077c59447413a7` valid;
- bootloader: 21328 байт, checksum `7d` valid, validation hash `13ebc5fa807944221e0296f07c9f26683954533b2612185e22dd89ee436f8638` valid;
- таблица разделов разобрана и проверена штатным инструментом ESP-IDF.

Официальная документация Espressif подтверждает для `ESP32-S3-DevKitC-1-N8R8` модуль `ESP32-S3-WROOM-1-N8R8`, 8 МБ flash и 8 МБ PSRAM:
https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32s3/esp32-s3-devkitc-1/user_guide_v1.1.html

Параметр `CONFIG_SPIRAM_MODE_OCT` соответствует штатному варианту `Octal Mode PSRAM` в ESP-IDF 5.5.2:
https://docs.espressif.com/projects/esp-idf/en/v5.5.2/esp32s3/api-reference/kconfig-reference.html#config-spiram-mode

## Запись штатным esptool

Из корня распакованного комплекта:

```sh
python -m esptool --chip esp32s3 -b 460800 \
  --before default_reset --after hard_reset write_flash \
  --flash_mode dio --flash_size 8MB --flash_freq 80m \
  0x0 firmware/bootloader/bootloader.bin \
  0x8000 firmware/partition_table/partition-table.bin \
  0x10000 firmware/sirokko_26201_controller.bin
```

Либо из каталога `firmware/`:

```sh
python -m esptool --chip esp32s3 -b 460800 \
  --before default_reset --after hard_reset write_flash @flash_args
```

Адреса и параметры взяты из свежего `flasher_args.json`, созданного ESP-IDF, а не переписаны из старого выпуска.

## Повторная сборка

`source/sirokko-26201-controller-0.1.4-full-source.tar.gz` сохранён без изменения. `build-config/` содержит фактические `sdkconfig`, `sdkconfig.defaults`, `dependencies.lock`, машинный `sdkconfig.json` и архив разрешённых зависимостей. В lock зафиксированы ESP-IDF 5.5.2, `espressif/ds18b20` 0.4.0 и `espressif/onewire_bus` 1.1.2.

```sh
mkdir work
tar -xzf source/sirokko-26201-controller-0.1.4-full-source.tar.gz \
  -C work --strip-components=1
cp build-config/sdkconfig build-config/sdkconfig.defaults \
  build-config/dependencies.lock work/firmware/
tar -xzf build-config/managed-components.tar.gz -C work/firmware
rm -rf work/firmware/build

docker run --rm \
  -v "$PWD/work/firmware:/workspace/firmware" \
  -w /workspace/firmware \
  espressif/idf@sha256:05cbfc42ed2e987b8026722c15bf1d8523d3e4fd1b4ac04d2e4056f5e0918b99 \
  idf.py build
```

Путь монтирования `/workspace/firmware` намеренно совпадает с путями, записанными официальным Component Manager в `dependencies.lock`.

Свежие бинарники отличаются от ранее вложенных в source-архив только метаданными времени сборки и производными checksum/hash: код, исходный commit, целевая конфигурация, размеры и версии зависимостей совпадают. Для прошивки использовать бинарники этого комплекта и их `manifest.json`/`SHA256SUMS`, не старый каталог `release/` внутри архивного снимка.

## Состав

- `firmware/` — свежие bootloader, partition table, app и реальные аргументы прошивки;
- `build-config/` — итоговые конфигурация, lock и точные исходники управляемых зависимостей;
- `source/` — неизменённый полный source-архив и его паспорт;
- `logs/` — полные журналы host-тестов, чистой сборки и проверки ESP-образов;
- `manifest.json` — машинный паспорт сборки;
- `SHA256SUMS` — контрольные суммы всех файлов комплекта.

## Непроверенные границы

Не выполнялись физическая прошивка, чтение обратно с платы, запуск TFT, датчиков, GPIO, общего SPI, watchdog/latch, силовых каналов, вентилятора, топлива, накала и горения. Не подтверждены реальные частоты/скважности PWM, температуры, задержки, токи и профили. До стендовой квалификации сохраняется запрет топлива и накала; скрытых обходов защиты в комплекте нет.
