# Проверки 0.1.7-DRAFT и непроверенные границы

Source `1e532416b1061be485981450088d2f469e6d7058`, BENCH ONLY. GPIO/hardware/согласованная архитектура не менялись; параллельная работа над PCB сохранена. Плата не подключалась, не прошивалась, испытаний горелки не было.

## Проверено реальным выполнением

| Проверка | Доказательство |
|---|---|
| Базовый автомат/output/NVS | 50/50, `logs/host-final.log`. |
| Термостат/профиль/учёт | 11/11: hysteresis/noise/calibration/raw/CRC/invalid/85/stale; run_low gate, нет самопуска, lease/счётчики/кольцо/uint64/повреждение/boot/history/countdown. |
| Production app tasks | 11/11: STOP на этапах/START в меню, OFF перед flash ack, START/lease, неисправный GPIO/storage/отказ ack, stalled UI/sensor/control, mark-valid/rollback. RTOS/отadata/периферия подменены. |
| Production display + serial-bus model | 4/4: фактические SDK clock/mode/CS1+1/20строк/16полос, кадр/охранные слова/DMA ownership, sensor-contention+STOP, stalled/error и fuel/glow OFF с продувкой. Не аппаратный SPI. |
| UI | 4/4; 12 кадров из production render-буферов, 13 PNG с contact sheet. Все 12 просмотрены; отсутствующих вторых строк/полос/обрезанной версии нет. 10 кадров побайтово как прежний layout, settings/version меняют номер версии. `logs/ui-verification.json`. |
| Production network adapter | 17/17: strict JSON, replay/nonce/TTL/retained/forbidden, signed manifest metadata/hash/version/target, BUSY без прерывания горения, SDK/TLS/disconnect/truncation/finish failure, exclusive lease до restart. OpenSSL RSA3072/SHA256 настоящий; TLS/Wi-Fi/MQTT/flash подменены. |
| Сервисная утилита | 4/4 Python: временная RSA3072 sign→request→production parser+verifier, изменённый hash, HTTP/version/seq, owner-only файл/нет перезаписи/печати значений. Временный ключ уничтожен. |
| Дополнительные START+RESET | 10/10 сравнений BOOT/RECOVERY/PREPURGE/PREHEAT/SPINUP/IGNITION/CONFIRM/RUN_HIGH/RUN_LOW/POST_PURGE; команды и этапы не изменяются от повторного START/RESET. `logs/extra-buttons.log`, исходник `verification/extra-buttons.cpp`. |
| Source после target build | Все 72 Git-файла на Lab побайтово совпали с исходным input tar; его SHA256=4139cc72dff69495f165b4d8e0d027a8ba4762438e0d8bdaa4891d16f6ae324e. `logs/native-source-readback.json`. |
| Source archive/повтор | 135/135 identity files, распакованный source прошёл те же 97 C+++4 Python, 13/13 PNG совпали с kit. `logs/host-from-package.log`, `logs/package-verification.json`. |
| Чистая ESP32-S3 сборка | Новый пустой каталог, fixed official ESP-IDF5.5.2, 1097 Ninja steps, exit0. `logs/target-build.log`. |
| Native image/разделы | App1058640 /0x102750; slots0x300000, free2087088 /0x1fd8b0. Flash8MB/DIO80MHz, OctalPSRAM80MHz, rollback=true; esptool version0.1.7-DRAFT/ESP32-S3/checksum+hash valid. Физическая память не читалась. |

10s serial-bus MODEL: 501 control pass, 250 спада heartbeat, max falling interval40ms, max MAX wait7625us, 3001 сериализованная транзакция. Оба MAX запрашиваются во время TFT DMA. Это модель номинальных transfer times/подменённого SDK, не измерение настоящих IRQ, режима шины, очередей IDF или возраста датчиков. Контрольные датчики в модели инжектированы; реальные freshness/DRDY/возраст/задержки требуют платы.

Полный чистый build log сохраняет optional `DS18B20_SENSOR_HUB` warning/автоматический reconfigure. Внешние компоненты не исправлены и не заменены; предупреждение не скрыто.

## Что исправлено, но не измерено физически

TFT20MHz (50ns) из 0.1.6 нарушал паспортный write cycle≥66ns и не является допустимым режимом по умолчанию. Теперь 10MHz (100ns), CS pre/post1+1, единый 9600-byte render/DMA буфер 240×20, 16 полос. Пиксельный wire7.68ms/полосу, 122.88ms/кадр. Timeout50ms сохранён, проверяется при очередном UI-проходе. Не подменять его абсолютным аппаратным пределом задержки.

`docs/tft_spi_timing_ru.md` разбирает реальный IDF путь `cs_ena_pretrans/posttrans`→HAL и D/C pre_cb до запуска SPI. Эти значения не доказывают CS-high≥40ns, D/C/data setup/hold≥10ns, Tr/Tf≤15ns, качество питания/проводки/дисплея. В LCD SPI API нет отдельной настройки CS-high. Без осциллограммы квалификация не выдана.

## Сохранённые программные ограничения

- Принятый диапазон PWM не является диапазоном устойчивого горения. Нет профиля по умолчанию; run_low_qualified — отдельный измеренный допуск, не пункт произвольного меню/MQTT.
- После STOP/питания/уставки/аварии/OTA нет автоматического повторного розжига. Pending OTA/flash lease не сохраняет отложенный START.
- Flash/restart запрещены при горении/розжиге/обязательной продувке. Применённый GPIO OFF подтверждает SDK-вызов, а не реальное размыкание залипшего контакта.
- NVS records целостны по отдельности, общего multi-key snapshot нет. Внезапное питание может потерять последние RAM-настройки/историю/моточасы и ещё не сохранённую аварию. Нет обещания полной сохранности без резервного питания/FRAM.
- Перед OTA checkpoint объединяет текущие и сохранённые faults, не превращает незавершённый RESET в очищенную аварию. Консервативно после reboot может потребоваться повторный квалифицированный физический RESET.
- Неизвестная/повреждённая критичная NVS блокирует START, не стирается автоматически. Schema1/2 не мигрируются молча; settings save не ремонтирует все ключи.
- Настоящий TLS handshake/ACL/сертификат/туннель/плата не проверены. Host проверяет передачу CA/hostname flags и отказ внешнего API, а не атаки на физическую сеть.
- Нет действующего owner private key/готового подписанного владельцем обновления, secure boot/eFuse antirollback. Подпись задания не защищает от физической перезаписи flash; незашифрованная NVS физически читаема.

## Физический стенд — НЕ ВЫПОЛНЕН, только без топлива

1. Проверить cold boot/память N8R8, питание/уровни, DS18B20/MAX31856/K-type: обрыв/CRC/85°C/stale и EXH/AIR mapping. Ошибку датчика не компенсировать поправкой.
2. Проверить настоящий TFT/кириллицу/цвет/ориентацию/SWRESET и пять кнопок/дребезг/удержание при boot. STOP первичен во всех меню/этапах; RESET — полный аппаратный протокол commissioning.
3. На выводах модуля измерить SCK10MHz/write≥66ns, CS setup/hold≥15ns/high≥40ns, D/C/DATA setup/hold≥10ns, rise/fall≤15ns. Подтвердить CS9/10/47 не одновременно, MAX mode1/5MHz и TFT mode0/10MHz, отсутствие порчи и реальный возраст датчиков при непрерывной загрузке не менее часа. Нет MISO/TE/presence у съёмного TFT: SDK success не доказывает изображение.
4. Проверить GPIO17 OFF→PWM→DC→PWM→OFF, границы1/800/1000, нет LOW-провалов на1000; команды FUEL/GLOW/PERMIT/ARM/SW_TRIP, силовые OFF и отказ SDK во всех моментах.
5. Измерить control/STOP/спадающий heartbeat под TFT/NVS/Wi-Fi/TLS и отказами. По commissioning: heartbeat≤100ms, цикл≤40ms. Остановить control: UI/сеть не кормят watchdog. Отдельно квалифицировать latch/watchdog/независимый выбег/обрыв проводов.
6. После полного останова проверить service lease/START/STOP/OTA/USB, реальные выходы, flash/SPI ошибки, отключение питания и повреждённые NVS. Никакого разрешённого самопуска.
7. С согласованными ключами/CA/ACL проверить реальный TLS/hostname/expired/invalid/wrong-auth, затем valid/unsigned/tampered/truncated/oversize/wrong-target OTA, потери сети/питания, оба слота/pending health/rollback. Каждый reboot — recovery/старые faults/профиль/новый локальный START.
8. Огневую квалификацию свечи/муфты/вентилятора, порогов/пламени и низкого режима проводить только отдельным разрешением. Этот комплект не является таким разрешением.

## Повтор дополнительной матрицы

Из распакованного work/firmware/, путь к комплекту заменить:

```sh
g++ -std=c++20 -Wall -Wextra -Werror -pedantic -O2 \
  -Icomponents/control_core/include -Imain -Itests -Itests/stubs \
  components/control_core/control.cpp components/control_core/button.cpp \
  components/control_core/operator.cpp components/control_core/service.cpp \
  components/control_core/ui.cpp main/output_driver.cpp main/commissioning.cpp \
  tests/idf_stubs.cpp /путь/комплект/verification/extra-buttons.cpp -o build-host/extra-buttons
build-host/extra-buttons
```

Перечисленные физические проверки не объявляются выполненными; допуск отопителя отсутствует.
