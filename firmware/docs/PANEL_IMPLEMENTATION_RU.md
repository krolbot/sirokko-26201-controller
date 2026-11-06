# Локальная панель Sirokko: ST7789 + EC11 + KEY0

## Граница реализации

Исходники панели разделены на переносимую модель `panel_ui.*` и ESP-IDF-адаптер `panel.*`.

- `panel_begin()` использует **SPI3_HOST** только для ST7789: MOSI GPIO7, SCK GPIO8, RES GPIO6, DC GPIO41, CS GPIO42.
- EC11 читается с GPIO5/GPIO38/GPIO47; KEY0 не читается панелью и остаётся отдельной задачей GPIO40.
- Панель не управляет топливом, свечой, вентилятором или PWM напрямую. Все изменения процесса идут только через `runtime_issue()` + `runtime_submit()` либо `runtime_stop()`.
- Задача `panel` имеет FreeRTOS priority 3; `control_task` с priority 20 не ждёт рендеринга, SPI или EC11.
- Исходное разрешение по умолчанию — 240×320. До проверки конкретного дисплея можно задать build-config макросы `CONFIG_SIROKKO_PANEL_WIDTH`, `CONFIG_SIROKKO_PANEL_HEIGHT`, `CONFIG_SIROKKO_PANEL_ROTATION` (`0/90/180/270`), `CONFIG_SIROKKO_PANEL_X_GAP`, `CONFIG_SIROKKO_PANEL_Y_GAP`, `CONFIG_SIROKKO_PANEL_INVERT`. Эти настройки не меняют GPIO-карту.

## Жесты

- KEY0: после стабильного LOW в течение 30 ms немедленно выполняется один START/STOP. Намерение фиксируется при первом LOW; переход состояния во время антидребезга не может превратить STOP в START. Низкий уровень при загрузке требует полного стабильного отпускания; удержание не даёт автоповтора.
- При `calibration_active` KEY0 вызывает `runtime_stop(true)`, во всех остальных случаях STOP использует `runtime_stop(false)`.
- HOME: поворот открывает редактор уставки, а в `MANUAL` — мощности. Один клик подтверждает редактор; клик без редактирования открывает меню.
- MENU: поворот выбирает пункт; один клик открывает/запускает; двойной клик возвращает на HOME.
- EDIT: одиночный клик сохраняется только после окна двойного клика 280 ms. Второй клик в окне отменяет черновик до какого-либо `runtime_submit()`.
- Выбор, черновик и отложенный одиночный клик аннулируются при внешней смене state/settings/fault; старое нажатие не применяется к другому экрану.

Пункты меню: режим, ручная мощность, температура 15…30 °C шаг 1 °C, «Выключить через» 0…10080 min, длительность нового запуска 0…10080 min, одноразовый отложенный пуск/отмена, автоматическая калибровка, диагностика, Wi‑Fi, инструкция OTA в браузере, назад. `TIMER` с `delay_ms=0` — явная отмена активного таймера.

## Локальные Wi‑Fi данные

`REQUEST_NETWORK_CREDENTIALS` вызывает только `runtime_network_credentials()` и выводит SSID/пароль на TFT. Строки обнуляются после передачи в модель; панель их не пишет в лог, NVS, host-тесты, PNG или сетевые ответы. При ошибке показывается локальное сообщение без значения секрета.

## Шрифт

`panel_font.*` содержит собственный bitmap 5×7 для русского uppercase, ASCII и отдельного lowercase ASCII (пароль сохраняет регистр). В заголовке и исходнике указан `SPDX-License-Identifier: MIT`; сторонних шрифтов или бинарных ассетов нет.

## Интеграция

Панель, шрифт и модель меню включены в `main/CMakeLists.txt`; `panel_begin()` вызывается после создания runtime и задач. Геометрия/ориентация доступны в `Kconfig.projbuild`. Callback KEY0 передаёт `calibration_active` и отрицательный операторский сигнал. Runtime обрабатывает TIMER/CANCEL_ARM/CALIBRATE/ARM/SETTINGS; успешная отправка в очередь не подменяет подтверждение автоматом. AP-пароль не выводится в serial log. Все три panel/KEY0 host-набора включены в общий `run_host_tests.py`, полная интегрированная ESP32-S3 сборка прошла.

## Проверяемые host-команды

Запускаются из `firmware/`:

```bash
g++ -std=c++20 -O2 -Wall -Wextra -Werror \
  -I components/control_core/include -I main \
  main/start_stop_button.cpp tests/test_start_stop_button.cpp \
  -o build-host/test_start_stop_button && build-host/test_start_stop_button

g++ -std=c++20 -O2 -Wall -Wextra -Werror \
  -I components/control_core/include -I main \
  main/panel_ui.cpp tests/test_panel_ui.cpp \
  -o build-host/test_panel_ui && build-host/test_panel_ui

g++ -std=c++20 -O2 -Wall -Wextra -Werror \
  -I main main/panel_font.cpp tests/test_panel_font.cpp \
  -o build-host/test_panel_font && build-host/test_panel_font

python3 tools/render_panel_ui_synthetic.py --output tools/panel_ui_synthetic.png
```

`test_start_stop_button` проверяет короткое нажатие, 30 ms anti-bounce, boot-held, single-shot, фиксацию намерения при смене состояния, блокировки, gap/clock regression и `runtime_stop(true)` при калибровке.

`test_panel_ui` проверяет FSM HOME/MENU/EDIT, отложенный одиночный клик, отмену двойным кликом до сохранения, TIMER cancel, CANCEL_ARM, CALIBRATE/error, отмену устаревшего ввода и debounce EC11.

`test_panel_font` проверяет UTF-8 lookup кириллицы, регистр ASCII и fallback.

## Синтетический осмотр

`firmware/tools/panel_ui_synthetic.png` — локальная композиция трёх экранов по 240×320 с синтетическими данными и явной подписью «СИНТЕТИЧЕСКИЙ МАКЕТ». Это предварительный визуальный осмотр, не результат TFT, огневого или физического испытания.
