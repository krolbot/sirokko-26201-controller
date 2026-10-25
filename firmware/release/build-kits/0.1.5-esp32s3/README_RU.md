# Sirokko 262.01 — комплект 0.1.5-DRAFT / BENCH ONLY

Это стендовый комплект для ESP32-S3-DevKitC-1-N8R8 v1.1, не разрешение топлива, накала или огневой эксплуатации. Плата не подключалась и не прошивалась. Силовой кандидат LTC7001 + IRFB4110 ещё не квалифицирован.

GPIO17: `0` — постоянный LOW; `1..800` — PWM; `1000` — постоянный HIGH через штатный `ledc_stop(..., 1)`, а не 1023/1024 PWM. `801..999` запрещены. Частота для фиксированных APB 80 МГц/10 бит — `77..20000` Гц. Старые недопустимые accepted NVS-профили не разрешают START. Максимальные этапы и штатная постпродувка сохранены как 100% DC. Ошибка выходного адаптера отдельно защёлкивает OFF до reboot. Подробности, источники API и невыполненный стендовый протокол — в `VERIFICATION_RU.md`.

## Проверка комплекта

Из корня распакованного комплекта:

```sh
sha256sum -c SHA256SUMS
```

`manifest.json` содержит реальные размеры/хэши, flash-параметры, число выполненных тестов и результаты сборки/разбора образов. `logs/host-tests.log` — 50/50: 40 исходных и 10 новых групп. Подмена API в host-тестах не является аппаратным измерением GPIO. В `logs/` сохранены полный журнал чистой целевой сборки ESP-IDF 5.5.2, разбор двух образов/таблицы разделов и три неудачные попытки: `idf.py -j` не поддерживается; старый lock требует реестр и не работает без сети; отключение Component Manager теряет транзитивный include onewire_bus в DS18B20 и отвергнуто. Окончательная сборка использует обычный Component Manager, исходящие запросы официальному реестру, reconfigure и CMake с явной параллельностью. Сторонние библиотеки не исправлялись.

Конфигурация: ESP32-S3, flash 8MB DIO 80MHz, Octal PSRAM 80MHz с инициализацией при загрузке. Это выбранная конфигурация SDK; реальная микросхема памяти не опрашивалась. Адреса из текущего `flasher_args.json`: bootloader `0x0`, partition table `0x8000`, app `0x10000`.

В успешном журнале сохранены предупреждение необязательного `DS18B20_SENSOR_HUB` из исходного компонента Espressif и штатный повтор конфигурации. После повтора необязательный sensor_hub пропущен; версии DS18B20 0.4.0 / onewire_bus 1.1.2 сохранены. Компоненты не исправлялись ради отсутствия предупреждения. Это предупреждение конфигурации, не ошибка компиляции проектного кода.

## Запись, только без топлива и накала

Из корня комплекта, с официальным esptool:

```sh
python -m esptool --chip esp32s3 -b 460800 \
  --before default_reset --after hard_reset write_flash \
  --flash_mode dio --flash_size 8MB --flash_freq 80m \
  0x0 firmware/bootloader/bootloader.bin \
  0x8000 firmware/partition_table/partition-table.bin \
  0x10000 firmware/sirokko_26201_controller.bin
```

Либо из `firmware/`:

```sh
python -m esptool --chip esp32s3 -b 460800 \
  --before default_reset --after hard_reset write_flash @flash_args
```

Ни одна из этих команд при подготовке комплекта не выполнялась с физической платой. NVS не стирается автоматически; старый профиль исправляется и принимается по `firmware/docs/commissioning_ru.md` из исходного архива.

## Чистая повторная сборка

Архив исходников содержит всю прошивку этой версии, host-тесты, конфигурацию и точные управляемые зависимости, но не hardware, Git-credentials или прежние release-архивы. `source/source-identity.json` задаёт SHA-256 всех исходных файлов и upstream base. Публикационный commit с исходником и этим комплектом проверяется отдельно: паспорт не содержит самоссылочного выдуманного commit.

```sh
mkdir work
# Распаковать в пустой каталог, никогда не поверх рабочей PCB.
tar -xzf source/sirokko-26201-controller-0.1.5-firmware-source.tar.gz -C work
cp build-config/sdkconfig build-config/dependencies.lock work/firmware/
# В исходном архиве уже присутствуют managed_components и их лицензии.
# Альтернативно восстановить их из build-config/managed-components.tar.gz.
(cd work/firmware && bash tests/run_host_tests.sh)

docker run --rm --cpus 2 --memory 4g --pids-limit 512 \
  --cap-drop ALL --security-opt no-new-privileges:true \
  -v "$PWD/work/firmware:/workspace/firmware" -w /workspace/firmware \
  espressif/idf@sha256:05cbfc42ed2e987b8026722c15bf1d8523d3e4fd1b4ac04d2e4056f5e0918b99 \
  bash -c 'idf.py --version && idf.py --no-ccache reconfigure && cmake --build build --parallel 2'
```

Образ должен быть заранее получен в разрешённом окружении. Нужен исходящий доступ к официальному Component Registry; обещания автономной сборки без сети нет. SDK на Hermes не устанавливался; в рамках этой задачи права и соседние службы не изменялись, порты не публиковались. В `build-config/dependencies.lock` лежит свежий результат штатного Component Manager с теми же точными версиями; в source-архиве сохранён первоначальный входной lock, который команда выше заменяет. Путь `/workspace/firmware` сохранён. Повторная сборка воспроизводит исходники, конфигурацию, зависимости и процесс; побайтовое равенство образа с новым временем компиляции не обещается. Для готовой записи применяются вложенные образы и их SHA-256.

## Управляемые зависимости и права

Неизменённый `build-config/managed-components.tar.gz` взят из ранее опубликованного 0.1.4 kit. Сохранены полные LICENSE и авторские/SPDX-уведомления:

- `espressif/ds18b20` 0.4.0, Apache-2.0: https://github.com/espressif/esp-bsp/tree/bf92b0b342510b3b2a1f47a8771619609c8636cc/components/ds18b20
- `espressif/onewire_bus` 1.1.2, Apache-2.0: https://github.com/espressif/idf-extra-components/tree/6b172553fdd4021ac79210fece111dc1b7e75b29/onewire_bus

Это сохранение условий сторонних компонентов, не новая лицензия на собственные материалы Sirokko. ESP-IDF взят из официального контейнера 5.5.2 с зафиксированным digest; его исходник не перепубликуется этим комплектом.

## Непроверенные границы

Физические GPIO/PWM/постоянные уровни, времена RTOS/CLI/NVS/TFT, watchdog/защёлка/RESET, SPI/датчики, память, силовые каскады, вентилятор, накал, топливо и горелка не испытаны. Обязателен осциллографический протокол из `VERIFICATION_RU.md`. Работоспособная компиляция не делает силовую схему квалифицированной и не открывает огневую эксплуатацию. Старый 0.1.4 комплект сохранён без изменений.
