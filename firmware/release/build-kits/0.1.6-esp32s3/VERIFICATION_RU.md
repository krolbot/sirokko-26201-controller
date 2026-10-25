# Проверки 0.1.6-DRAFT и непроверенные границы

Статус: реализация стендового кандидата, не квалификация отопителя. Исходник main `4c8f02580a8403fff48621e4b108686627cd75e0`; исходная прошивка 0.1.5 — `8aed22e82b87199476af3462d34f7c29e780fda8`. Аппаратные правки параллельного commit `0f53b80` сохранены, эта работа не меняла hardware/GPIO/архитектуру.

## Выполнено с реальным выводом инструментов

| Проверка | Результат / доказательство |
|---|---|
| Существующая регрессия автомата/output/NVS | 50/50, включая все исходные группы; `logs/host-final.log`. |
| Новые термостат/профиль/учёт/NVS | 11/11: границы гистерезиса, шум, invalid/CRC/stale/default85 и поправка, нет автопуска, run_low gate, lease во всех состояниях, кольцо/uint64 saturation/повреждение/старые схемы/boot ID/восстановление истории, countdown. |
| Production app_main/control/storage/health задачи | 11/11: STOP по этапам розжига, START внутри меню, GPIO OFF до ack, новый START против lease, местный ремонт uncommissioned, отказ GPIO/timeout ack, stalled UI/sensor, storage corruption/commit failure, mark-valid/rollback/stalled control. Scheduler/peripherals/otadata подменены. |
| UI | 4/4: границы/дребезг/удержание/boot/таймаут/новое подтверждение сброса/формат полос; 12 PPM→PNG общего рендерера. Визуально просмотрены кириллица/обрезание/контраст, включая fault и подтверждение сброса. |
| Production service_network adapter | 17/17: retained/replay/expiry/nonce reconnect/forbidden operations, реальный JSON parse+queue, unsigned/wrong signature/metadata/hash, BUSY, oversize, TLS failure branch, disconnect/truncation/SDK finish error, version/project/target mismatch, безопасный restart lease, ограничение JSON nesting и telemetry. RSA-3072/SHA-256 настоящие OpenSSL; Wi-Fi/MQTT/TLS/HTTP/RTOS/flash — внешние подмены. |
| Сервисная утилита | 4/4 Python: настоящий временный RSA-3072 ключ, sign→request→production parser+verifier, rejection изменённого SHA; права 0600/отказ перезаписи, HTTP/version/seq rejection, защищённый USB-файл без печати значений. Временный ключ уничтожен. |
| Дополнительная матрица START+RESET | 10/10: BOOT_SELF_TEST, RECOVERY_PURGE, PREPURGE, PREHEAT, SPINUP, IGNITION_TRIAL, FLAME_CONFIRM, RUN_HIGH, RUN_LOW, POST_PURGE сравниваются с неизменённым экземпляром Machine. Входы не сокращают этапы/продувку и не изменяют команды. Источник probe — `verification/extra-buttons.cpp`, журнал — `logs/extra-buttons.log`. Это не физический debounce/электрическое измерение кнопки. |
| Полная регрессия из готового source archive | Тот же полный набор 93 C++ + 4 Python; `logs/host-from-package.log`. Повторные render PNG должны совпадать побайтово. |
| Чистая целевая сборка | Новый пустой каталог на Lab, официальный фиксированный контейнер ESP-IDF 5.5.2, reconfigure + 1097 Ninja steps, exit 0. Полный `logs/target-build.log`; не повторный запуск старого build. |
| Размер/SDK конфигурация | App `0x1026a0` / 1058464 bytes; ota_0/ota_1 по `0x300000`, свободно по `0x1fd960` / 2087264 bytes. Flash8MB/DIO80MHz, OctalPSRAM80MHz boot-init/memtest, rollback=true. Физическая память не опрашивалась. |
| Образы и разделы | Официальные esptool image_info: checksum/hash valid, ESP32-S3 chip ID9, проект sirokko_26201_controller, version0.1.6-DRAFT, ESP-IDFv5.5.2. gen_esp32part декодирует фактическую partition-table.bin; flasher_args ссылается на четыре существующих файла. |

Чистая сборка сохраняет предупреждение `DS18B20_SENSOR_HUB` из официального компонента и штатный автоматический reconfigure. sensor_hub после повтора пропущен, DS18B20/onewire versions не менялись и внешняя зависимость не исправлялась. Предупреждения не скрыты.

## Важные программные ограничения

- PWM электрический диапазон ≠ квалифицированный диапазон горения. По умолчанию нет accepted profile, `run_low_qualified` требует измеренного инженерного решения; новый параметр не разрешается просто из меню/MQTT.
- После STOP/уставки/питания/аварии/OTA нет автоматического повторного розжига. Старый START во время lease потребляется и не становится отложенным пуском. Pending OTA не принимает START до health decision.
- Перезапуск/flash не допускаются во время RUN, розжига и необходимой продувки. Применённый GPIO OFF — подтверждение успешного SDK-вызова, не независимое измерение реле. Залипшие контакты определяет аппаратная защита/испытания, не эти тесты.
- Отложенные NVS-снимки пишутся после безопасного останова, каждая запись целостна отдельно; общего multi-key snapshot нет. Внезапное питание может потерять последние RAM-настройки/историю/моточасы и ещё не сохранённую аварию. Это ограничение без внешней FRAM/резервного питания, не обещание полного журнала всех событий.
- Перед OTA выполняется отдельный checkpoint: текущие faults объединяются с ещё сохранёнными, до и после скачивания. OTA не очищает старую аварию/неуспевший RESET; в консервативном случае после reboot понадобится повторный квалифицированный физический RESET.
- Неизвестные/повреждённые критичные NVS records не стираются автоматически и блокируют START. Schema1/2 не повышается автоматически. `settings save` — только явное восстановление операторской записи, не исправление всех ключей.
- Неверные credentials/cert проверены как отказ внешнего API/недоступное подключение и правильная передача CA/hostname flags, НЕ как настоящий handshake со злонамеренным брокером. Реальный ESP-IDF TLS stack/сертификат/ACL/туннель здесь не испытаны.
- Нет owner private key в комплекте/плате, готового подписанного владельцем обновления, secure boot или eFuse antirollback. Подпись проверяет доверенное приложением задание, а не защищает от физической замены flash. Секреты сети в незашифрованной NVS физически читаемы.

## Обязательный физический стенд — НЕ ВЫПОЛНЕН

Без топлива/накала, с отдельным безопасным источником питания, измерительными приборами и согласованным протоколом:

1. Проверить cold boot, память N8R8, датчики DS18B20/MAX31856/K-type, обрыв/CRC/85°C/устаревание и правильную привязку EXH/AIR. Ошибку датчика не компенсировать поправкой.
2. Проверить настоящий TFT (кириллица/цвет/ориентация/SWRESET), все пять кнопок и debounce. Удержанные START/RESET/SET при boot не создают команды. STOP имеет приоритет во всех этапах/меню. Реальный RESET — полный аппаратный протокол из commissioning.
3. Логическим анализатором подтвердить раздельные CS9/10/47 на SPI2, mode1/5MHz MAX и mode0/20MHz TFT, отсутствие порчи/таймаутов при одновременной работе; не менее часа по commissioning. У снятого семиконтактного TFT нет сигнала присутствия — программный success не доказывает наличие картинки.
4. Осциллографом проверить OFF→PWM→DC→PWM→OFF GPIO17, частоты/границы 1/800/1000, отсутствие LOW-провалов при 1000; останов/ошибка в каждый момент. Проверить уровни FUEL_CMD/GLOW_CMD/FUEL_PERMIT/ARM/SW_TRIP и отсутствие аппаратного топлива после ошибки.
5. Измерить основной цикл/STOP и спадающие heartbeat под TFT/NVS/Wi-Fi/TLS нагрузкой и отказами; интервал heartbeat ≤100мс, основной цикл ≤40мс по commissioning. Остановить control: сеть/UI не должны кормить watchdog. Проверить независимую защёлку/watchdog/выбег и обрыв проводов.
6. После полного останова квалифицировать service lease: одновременный START/STOP/OTA/USB, контроль действительных выходов, отказ flash/SPI, прерывание питания при записях и повреждённые/неизвестные NVS. Ничто не должно создавать разрешённый пуск.
7. С настоящими ключами/CA/ACL проверить успешный TLS и отказ wrong credentials/invalid/expired/hostname cert. Затем valid/unsigned/tampered/truncated/oversize/wrong-target OTA, потери Wi-Fi/питания, оба слота, pending health и настоящий rollback. После каждого reboot — recovery, сохранённые аварии/профиль и новый локальный START.
8. Лишь отдельным разрешением квалифицировать силовую часть, свечу/муфту/вентилятор, времена/температуры/пламя и устойчивый низкий режим на конкретном отопителе. Чужие пороги/тайминги в качестве испытанных параметров не принимать.

## Повторить дополнительную матрицу

После распаковки source в `work/`, из `work/firmware/` (путь к комплекту заменить своим):

```sh
g++ -std=c++20 -Wall -Wextra -Werror -pedantic -O2 \
  -Icomponents/control_core/include -Imain -Itests -Itests/stubs \
  components/control_core/control.cpp components/control_core/button.cpp \
  components/control_core/operator.cpp components/control_core/service.cpp \
  components/control_core/ui.cpp main/output_driver.cpp main/commissioning.cpp \
  tests/idf_stubs.cpp /путь/комплект/verification/extra-buttons.cpp -o build-host/extra-buttons
build-host/extra-buttons
```

Данный комплект не объявляет перечисленные физические проверки выполненными и не разрешает эксплуатацию горелки.
