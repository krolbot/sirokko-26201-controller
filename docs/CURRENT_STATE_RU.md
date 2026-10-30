# Sirokko 262.01 — текущее состояние

**DRAFT / WIP / NOT FOR ORDER / NO POWER / NO FLASH / NO FIRE.**

Производственного выпуска нет. Плату нельзя заказывать, включать, прошивать или подключать к горелке.

## Единственный рабочий CAD

Актуальный парный проект находится в `hardware/kicad/`:

- `Sirokko26201_control_motor_candidate.kicad_pro`;
- `Sirokko26201_control_motor_candidate.kicad_sch`;
- `Sirokko26201_control_motor_candidate.kicad_pcb`;
- семь реально подключённых дочерних схем, таблицы библиотек, проектные символы, посадочные места и локальные 3D-модели из того же каталога.

Подключённые дочерние схемы:

- `MinimalSafety.kicad_sch`;
- `Sirokko_Input_Protection_WIP.kicad_sch`;
- `logic_power_24_5_DRAFT.kicad_sch`;
- `motor_external_hc160.kicad_sch`;
- `tft_power_tps62162_standalone_DRAFT/tft_power_tps62162_review_DRAFT.kicad_sch`;
- `tft_reset_local_DRAFT/sirokko_tft_reset_local_DRAFT.kicad_sch`;
- `tft_spi_isolation_DRAFT/sirokko_tft_spi_isolation_DRAFT.kicad_sch`.

`motor_pwm_REBUILD.kicad_sch` в действующую иерархию больше не входит и из рабочего дерева удалён.

Контрольные суммы принятого снимка:

- PCB: `b11c63de29efd64ce23d33bcd5e74f7ddecd919ec385467222ebf0c1102e6cf3`;
- PRO: `b96549e44738f1b88fdbd3e53116f548035f9d95ae738758a1296ec2ba1c6cba`;
- корневая SCH: `13d2cff5e4ab5796b1ceb31a73a1fb4faa6f5b0473d41c99cf0f51cd108fd5f5`;
- `MinimalSafety.kicad_sch`: `6dbfe60fac09dc172c0ecebb8c919383b986ff3bc48b9bfafbd8f36068e18de9`;
- `Sirokko_Input_Protection_WIP.kicad_sch`: `4f28fb49c3f5376521eb1ed7cc5bc393c5b09fe9b8da7a624fbefbc97213fd9f`;
- `logic_power_24_5_DRAFT.kicad_sch`: `78ac42d133c983e4abaff2181f09a9a25bf14318d6a6cd2459556b4c9557178c`;
- `motor_external_hc160.kicad_sch`: `9d45496fadaae133e40830adab1cc0409765af95a25f1f3d578ee33ae0092b91`;
- `tft_power_tps62162_review_DRAFT.kicad_sch`: `48995db43321d65b2b03f23e9d54c8773da89d2c4b2a6b58ea064d1378f992b6`;
- `sirokko_tft_reset_local_DRAFT.kicad_sch`: `2361488e7841f5f620f03a9cb01d755c1dfd112f457f7906f9c0c3dd4b1c49c2`;
- `sirokko_tft_spi_isolation_DRAFT.kicad_sch`: `bc646557b5bff1019f3773e4caf0848b1bd0f5c0f3fb10176979430bf264c370`.

## Внешний HC-160A-S2

Встроенный силовой мост двигателя и 28 его компонентов удалены. HC-160A-S2 — отдельный внешний модуль: на основной PCB нет его посадки, крепления или радиатора. Оба провода двигателя и силовое питание подключаются непосредственно к HC-160A-S2.

J29 на Sirokko передаёт только управление:

- J29.1 → A1: `/FAN_REQUEST`;
- J29.2 → A2: `/GND`;
- J29.3 → PA: `/SPEED_CMD`;
- J29.4 → G: `/GND`.

Выход `+5V OUT` модуля не используется. Подробности и ограничения приведены в `hardware/kicad/HC160_INTERFACE_RU.md`.

`J1.11/GPIO18` теперь NC. Возвращать RESET, TPS3431, защёлку или прежний встроенный моторный каскад нельзя.

## Плата и проверки

Текущая PCB содержит 117 посадочных мест, 439 физических площадок, 1357 сегментов и 419 переходных отверстий. После проверки связности удалены три изолированных переходных отверстия; потерянных и добавленных связей и новых обрывов не появилось. Один хвост +3V3 укорочен с сохранением средней точки соединения.

KiCad 10.0.7 без записи исходников успешно выгрузил список цепей парного проекта; ERC — 0 нарушений. Повторная DRC без перезаливки зон: 0 несоединённых элементов, 0 расхождений PCB↔SCH и 0 нарушений уровня ошибки. Общая DRC остаётся FAIL с кодом 5 и 51 неподавленным предупреждением: 46 `track_dangling` и 5 `copper_sliver`. Это не чистое прохождение DRC.

Производственные Gerber/Drill/BOM/CPL не выпускались. Не завершены проверка токов и нагрева, силовой меди, динамики защит, механики, жгута и физической работы.

## Экран и прошивка

Waveshare 18366 больше не выбран пользователем. Новый экран находится в подборе; 12219 — только кандидат, окончательный способ крепления не подтверждён. Существующая геометрия экрана в WIP-проекте не считается утверждённой заменой.

Согласованной с новым аппаратным контрактом прошивки нет. Прежнюю прошивку, которая управляет GPIO18 или предполагает встроенный моторный каскад, применять нельзя. Подачи питания, прошивки и огневых испытаний не было.
