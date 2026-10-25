# Sirokko 0.1.8-DRAFT — стендовый комплект DRAFT-06

BENCH ONLY. НЕ ПРОШИВАТЬ НЫНЕШНЮЮ PCB И СТАРУЮ ПЛАТУ С ВНЕШНИМ NC STOP. LOW GPIO8 шунтирует такой контакт. Этот комплект не разрешает питание опасных нагрузок, производство или огневые испытания. Плата не подключалась и не прошивалась.

## Что находится в комплекте

- Настоящие результаты ESP-IDF сборки ESP32-S3: приложение, bootloader, partition table, начальная otadata, flash_args и flasher_args.json в firmware/.
- Полный проверенный вход сборки в source/firmware-source.tar.gz: 76 Git-файлов из commit 3ef88c6cbd13ba83593f9e24cf222d2f79f0906b, sdkconfig/dependencies.lock и исходники управляемых компонентов. Это исходный снимок на момент сборки; его README ещё сообщает, что комплект готовится. Более новые инженерные дополнения и данный отчёт лежат отдельно и не подменяют исходную идентичность.
- ESP-IDF 5.5.2, commit 30aaf64524299d3bde422ca9a2848090d1bc5d0f; pinned image и ограниченный рецепт в build-config/compose.yaml. Ничего не устанавливалось на хост Hermes и не менялись соседние службы.
- Реальные журналы, результаты host-проверок, метаданные образов, SHA256SUMS, manifest.json. UI-картинки — программная симуляция, не фотографии дисплея.
- Исходный DRAFT-06 и дополнение NKK в docs/four_button_contract_v06/. Дополнение опубликовано c0969efb6de806dac5845e2701c98f2ecd88140c. Сторонние PDF и их изображения не включены.

## Несовместимость и ручная приёмка

Четыре кнопки: START_STOP GPIO7, SET_UP GPIO39, SET_DOWN GPIO40, RESET GPIO6. GPIO8/J1.12 — MCU_STOP_N input/output open-drain с внешней R15. Сначала безопасные FUEL/GLOW/PERMIT/ARM, затем released=1. При запросе ARM — OD LOW, подтверждение входной площадки следующим проходом, только потом ARM/SW_TRIP. Ошибки уровня/API защёлкивают отказ. Короткое нажатие — только UI; 1500 мс long после debounce 30 мс — ручной START либо штатный STOP с обязательной продувкой. Независимого мгновенного ручного NC STOP больше нет.

Старые принятые NVS/schema 3 не мигрируются в разрешение нового выхода. Нужны schema 4 и явный hardware_contract_id=6, ручная сверка новой проводки, отсутствие J22/старого NC STOP и новый инженерный ввод параметров. Автоматического обнаружения ревизии PCB нет. Пример host-профиля и legacy fixture не являются параметрами для устройства. Не стирать NVS как способ обхода защит.

NKK BB25AB-FA — функциональный кандидат RESET с документированным nonshorting/BBM, но ChipDip, сторона PC-сетки, монтажные ножки, посадка и полная квалификация HOLD. Кнопка не установлена, BOM/PCB не изменены. Порядок A/B и числовые времена не гарантированы. GPIO6 недостаточно: необходим SAFETY_OK, а после RESET требуется новый ручной START. Power-up LOW glitch GPIO8 typ 60 нс не исключён программной инициализацией; нужны измерения реальной safety-chain при всех видах питания/reset/прошивки.

## Повторение проверок без платы

Проверка целостности: python3 verify.py

Распаковать исходник и cJSON из комплекта:

    mkdir rebuild host-deps
    tar -xzf source/firmware-source.tar.gz -C rebuild
    tar -xzf build-config/cjson-source.tar.gz -C host-deps
    SIROKKO_CJSON_DIR="$PWD/host-deps/cJSON" bash rebuild/firmware/tests/run_host_tests.sh
    python3 rebuild/firmware/tools/render_previews.py rebuild/firmware/build-host/ui
    bash verification/run_reset_skew.sh "$PWD/rebuild/firmware"

Нужны уже установленные g++, cc, OpenSSL headers/libcrypto и Python. --core-only не заменяет полный набор. Дополнительный RESET-тест проверяет восемь временных сценариев и 32 сочетания независимых барьеров; это не восемь статических случаев из инженерного CSV.

Для настоящей чистой target-сборки в уже установленном Docker с указанным образом:

    cp build-config/compose.yaml rebuild/compose.yaml
    cd rebuild
    docker compose -p sirokko-018-rebuild run --rm idf

Источник не содержит предыдущего build-каталога. Версии компонентов и настройки фиксированы. Бинарная побайтная воспроизводимость не заявляется: ESP-IDF включает время компиляции. Повторение должно дать работающую сборку и те же контракты/настройки, не обязательно тот же SHA образа.

## Файлы прошивки — только для будущего разрешённого стенда

Использовать только offsets из вложенных flash_args/flasher_args.json, не старый offset приложения 0x10000. Получено: bootloader 0x0, partitions 0x8000, otadata 0x10000, app 0x20000; ota_1 0x320000. Приложение 1060544 байт, каждый app-слот 3145728 байт, свободно 2085184 байт. Flash 8 MB/DIO/80 MHz, Octal PSRAM 80 MHz, rollback включён. Это конфигурация сборки, не чтение установленного чипа.

Команда физической прошивки здесь намеренно не запускается. До неё инженер должен интегрировать аппаратуру и отдельно разрешить безопасный стенд. Реальные GPIO/SPI/TFT/датчики/реле/питание, watchdog/latch, Wi-Fi/TLS/MQTT/OTA/rollback и огневой режим не испытаны. Прежние 213 неразведённых, четыре предупреждения и пять ignored_checks PCB не сняты.
