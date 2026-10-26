# Проверка 0.1.9-DRAFT — программные результаты и границы

## Реальное выполнение

- Host compiler C++20, -Wall -Wextra -Werror -pedantic: core50/50, features11/11, app runtime11/11, display4/4, four-button13/13, timer11/11, UI4/4, network17/17. Всего121/121 групп, Python4/4. Network не пропущен: cJSON взят из прежнего SDK-комплекта, OpenSSL настоящий.
- Полный повтор tests/run_host_tests.sh из source/firmware-source.tar.gz: тот же121/121 +4/4. Все140 входных файлов повторно сверены, 77 Git-исходников связаны с точным удалённым commit через GitHub tree/blob SHA и SHA256 содержимого.
- Target: чистый build-registry, существующий pinned ESP-IDF5.5.2/SDK commit30aaf64524299d3bde422ca9a2848090d1bc5d0f, 2CPU/4GB, --pull=never. Exit0. Приложение1065536байт; OTA 2×3145728, запас2080192байта каждый. esptool checksum/hash valid для app и bootloader, binary partition table декодирована официальным SDK.
- 22 кадров production render_ui_band и contact sheet просмотрены. Правки надписей разделяют задержку-настройку/взведение, работу/остаток, выключение таймера/топлива. Все23 PNG побайтно совпали после повтора.
- Дополнительно выполнен один ограниченный read-only обзор. Найдена и устранена гонка старого snapshot автосохранения со взведением: отдельный AUTOSAVE проверяется управляющей задачей на текущем состоянии и не отменяет осознанный delay. Воспроизводимые сценарии включены в timer_tests.cpp. Явные OTA/local service по-прежнему отменяют delay, в том числе при отклонённой lease.

## Новые timer-группы и матрицы внутри них

1. real_app_duration_OFF_1m_1h_24h_before_at_after_wrap: OFF/1мин/1ч/24ч, до/на/позже дедлайна, обычный uptime/переполнение uint32, розжиг входит в срок.
2. real_app_duration_expiry_all_seven_active_states: PREPURGE/PREHEAT/SPINUP/IGNITION_TRIAL/FLAME_CONFIRM/RUN_HIGH/RUN_LOW → POST_PURGE, fuel/glow/permit OFF, вентилятор не выключен.
3. real_app_delay_1m_1h_24h_then_independent_duration_no_repeat: новый long, удержание без повторов, задержка1мин/1ч/24ч, граница/wrap/поздний проход, независимая длительность и отсутствие нового пуска при новом тепловом запросе.
4. real_app_fresh_blockers_and_STOP_RESET_FAULT_OTA_service_deadline_priority: десять изменений условий после взведения непосредственно на due-pass — remote STOP, raw RESET, safety fault, OTA request, local service, config/settings, выходной отказ, комнатная температура, горячая установка. Запрос погашен, скрытого повторения нет.
5. real_app_long_cancel_hold_no_purge_and_new_explicit_rearm: отдельное локальное снятие с взвода, холодный cancel без лишней продувки, held не повторяет, новое явное взведение.
6. real_app_physical_menu_staged_apply_DOWN_short_cancel: настоящие GPIO-последовательности меню/часы/минуты/UP Apply/DOWN cancel/short exit, без подмены состояния меню.
7. real_app_editor_STOP_FAULT_local_long_and_restart_now: STOP/FAULT/local long из нового редактора; то же значение явно перезапускает длительность NOW; Apply не поглощает уже начатое долгое STOP в активной сессии.
8. core_staging_OFF_expired_apply_and_STOP_FAULT_priority: редактирование без Apply сохраняет deadline; OFF не меняет топливо; новый срок NOW; истёкший старый deadline выигрывает у Apply, STOP/FAULT — у таймера.
9. NVS_legacy_newer_corrupt_bounds_history_and_reboot: настоящий старый бинарный layout и digest, validated migration v1→v2 с таймерамиOFF, сохранение старого ключа, bounds/newer/corrupt отказ, append-only TIMER history и отсутствие восстановления активных отсчётов/RUN.
10. real_app_autosave_stale_snapshot_qualification_and_deadline_races: autosave после старого снимка, во время квалификации LONG, после arm и на дедлайне. Непосредственный control-path отказывает AUTOSAVE, а не оператору.
11. menu_independent_fields_bounds_wrap_timeout_and_fresh_post_apply_LONG: независимые часы/минуты и24:00; тайм-аут30с с wrap не применяет правку; удержание START до Apply не считается новым взведением, отпускание/новый long необходимы.

Прежние suites дополнительно покрывают stale/quality/overtemp датчиков, сброс, аппаратную защёлку как модель, ошибки SDK/OD, STOP по стадиям, восстановление, ограниченную историю, безопасные NVS/OTA lease, stalled UI/control, nonce/replay/TTL, реальную RSA/SHA и отклонение MQTT start/reset/таймерных операций.

## Время и overflow

Production ms() берёт esp_timer_get_time()/1000: монотонный64-битный uptime ESP-IDF; управляющий автомат использует младшие32 бита и unsigned now-start. Максимум1440×60000=86400000мс меньше2^31, поэтому разность и дедлайн переходят через uint32 wrap корректно. Умножение выполняется в uint32. Остаток насыщается на0. Исполнение — в первом управляющем проходе после достижения срока, без блокирующего ожидания и без RTC/NTP. При late-pass старый expiry проверяется до Apply. Полный пропуск управляющей задачи на период оборота uint32 не поддерживается: реальный независимый watchdog должен снять разрешение гораздо раньше.

Тестовый ускоренный планировщик не ждёт часы. Поздний виртуальный проход проверяет вычисление, а НЕ разрешает пятисекундный отказ реального control/watchdog. Модель GPIO/RTOS/датчиков не измеряет электрические времена и не доказывает безопасность аппаратуры. Существующая SPI-модель сообщает501control-pass/250спадов за10виртуальных секунд, max40мс, MAX wait7625мкс — это модель, не осциллограмма.

## Сборка и окружение

Попытка target с полностью отключённой сетью воспроизведённо остановилась на metadata Component Registry. Попытка штатного IDF_COMPONENT_MANAGER=0 + EXTRA_COMPONENT_DIRS нашла компоненты, но их YAML dependency ds18b20→onewire_bus без manager не добавилась: onewire_bus.h not found. Это не ошибка таймеров и не основание править внешнюю библиотеку. Успешная сборка использует обычный официальный manager и исходящий registry, как предыдущий комплект. Все61 файла прежних компонентов побайтно сохранены; поменялась только разрешённая manager информация в dependencies.lock. Локальный Docker-образ не скачивался, пакеты на Hermes/Lab не устанавливались. Optional DS18B20_SENSOR_HUB warning и штатный повтор Kconfig видны в журнале; sensor_hub не включён. Офлайн/побайтовая воспроизводимость новой target-сборки не заявлены.

## Не выполнено

Физическое подключение/прошивка платы, подача питания на опасные нагрузки или топливо; измерения интерфейсов/снятия топлива; реальная сеть и OTA/откат/обрыв питания; ЭМС/тепловая/закупочная и производственная квалификация. Сервисы, авторизация, GPIO, инженерный профиль/schema4/hardware_contract_id6 и аппаратный pinmap не менялись. Нынешняя PCB STOP_STATUS не совместима с MCU_STOP_N firmware; RESET/колодки и силовые цепи остаются HOLD. Документальные RTC-кандидаты сохранены, в firmware RTC/календарный автопуск не добавлены. BENCH ONLY / NOT FOR ORDER / NO FIRE.
