# Пусконаладка прошивки

## Безопасное исходное состояние

Новая NVS не содержит принятой конфигурации. Ядро фиксирует `FAULT_CONFIG`, поднимает `SW_TRIP` и держит `GLOW_CMD`, `FUEL_CMD`, `FUEL_PERMIT` выключенными. Это штатное поведение.

USB-CLI — только стендовый интерфейс. Конечное управление выполняется локальными кнопками и OLED без сети. Команда CLI `reset` не воздействует на физический reset latch и не заменяет кнопку RESET.

## Обязательные проверки до ввода параметров

1. Не подключать топливо и силовые нагрузки.
2. Проверить общую землю логики и внешние подтяжки из `io-contract.json`.
3. Проверить, что boot/reset/обрыв держат `ARM_READY=0`, `FUEL_CMD=0`, `FUEL_PERMIT=0`.
4. Проверить STOP: замкнутый НЗ контакт даёт LOW, нажатие или обрыв — HIGH.
5. Проверить active-low кнопки и то, что удержание START/RESET при загрузке не создаёт событие.
6. Сымитировать каждый MAX31856 fault и DS18B20 timeout/CRC/85 °C; оба топливных канала должны оставаться выключенными.
7. Проверить OLED: потеря обязательного локального дисплея переводит автомат в LOCKOUT.
8. Проверить внешний watchdog, set-dominant latch и независимый аппаратный выбег отдельно от программы.

## Команды

```text
status
faults
config show
config set <field> <integer>
config set revision A|B
config validate
config token
config accept <8-hex-token>
config revoke
setpoint <celsius>
reboot
```

Температуры в `config set` задаются в миллиградусах Цельсия, времена — в миллисекундах, PWM — в тысячных долях `0..1000`.

Поля конфигурации:

- времена: `boot_self_test_timeout_ms`, `prepurge_ms`, `preheat_ms`, `spinup_ms`, `ignition_window_ms`, `flame_confirm_ms`, `postpurge_min_ms`, `stage_min_ms`, `restart_inhibit_ms`, `glow_max_ms`, `sensor_stale_ms`, `heartbeat_toggle_ms`;
- пределы датчиков: `sensor_min_milli_c`, `sensor_max_milli_c`, `exhaust_overtemp_milli_c`, `air_overtemp_milli_c`, `cool_exhaust_milli_c`, `cool_air_milli_c`, `hot_start_exhaust_milli_c`, `hot_start_air_milli_c`;
- пламя: `flame_rise_milli_c`, `flame_min_exhaust_milli_c`, `flame_hold_exhaust_milli_c`;
- термостат: `setpoint_min_milli_c`, `setpoint_max_milli_c`, `setpoint_default_milli_c`, `start_delta_milli_c`, `high_to_low_delta_milli_c`, `low_to_high_delta_milli_c`, `stop_delta_milli_c`;
- `low_pwm_permille`: измеренный низкий режим REV-B; в текущем DRAFT он также задаёт обдув во время PREHEAT. REV-A использует отдельный дискретный вход низкой скорости.

Любое изменение поля автоматически отзывает приёмку. `config token` вычисляет контрольный токен всего кандидата. `config accept` принимает конфигурацию только при точном совпадении токена и нулевом результате валидатора. После принятия нужна перезагрузка.

## Последовательность приёмки

1. Ввести все значения только по результатам измерений конкретного стенда.
2. Выполнить `config show`, сохранить вывод в протокол испытаний.
3. Выполнить `config validate`; требуется `candidate_errors=00000000`.
4. Выполнить `config token`, независимо сверить значения.
5. Выполнить `config accept <token>` и `reboot`.
6. Без топлива проверить состояния PREPURGE/PREHEAT/SPINUP: вентилятор не должен прерываться; при PREHEAT топливо выключено.
7. Проверить STOP и каждый аварийный вход во всех активных состояниях.
8. Огневые испытания выполнять только по отдельному утверждённому стендовому протоколу.

Ручной сброс latch считается завершённым только после: устранения причины; выключенных топлива/накала; отпущенного START; замкнутого STOP; свежих датчиков; `ARM_READY=1`; удержания физического RESET до восстановления `SAFETY_OK=1`; последующего отпускания RESET. После сброса автомат ждёт отдельного нового START.
