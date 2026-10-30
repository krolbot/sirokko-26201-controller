# Sirokko 262.01 — контроллер автоматики

> **DRAFT / NOT FOR ORDER / NO POWER / NO FLASH / NO FIRE.** Плата не закончена, не прошла производственную и физическую приёмку и не предназначена для заказа, подачи питания, прошивки или работы с горелкой.

В рабочем дереве оставлен один актуальный проект KiCad:

- проект: [`hardware/kicad/Sirokko26201_control_motor_candidate.kicad_pro`](hardware/kicad/Sirokko26201_control_motor_candidate.kicad_pro);
- схема: [`hardware/kicad/Sirokko26201_control_motor_candidate.kicad_sch`](hardware/kicad/Sirokko26201_control_motor_candidate.kicad_sch);
- плата: [`hardware/kicad/Sirokko26201_control_motor_candidate.kicad_pcb`](hardware/kicad/Sirokko26201_control_motor_candidate.kicad_pcb);
- SHA-256 текущей PCB: `a9d5a3e2499aaa576a926920e9e9029b702352795da7f7ec8a7a0baaa690cda3`.

Текущая PCB содержит 144 посадочных места, 528 физических площадок, 1600 сегментов, 469 переходных отверстий и 38 зон. `GPIO18 = J1.11 = /OCP_OK`: цифровой вход Hi-Z, где LOW означает аппаратное событие токовой отсечки. MCU не должен переводить его в выходной режим или включать внутреннюю подтяжку вниз. Внешняя подтяжка R9008 к +3V3 уже есть; вторую не добавлять. Аппаратная отсечка через U9003 и U9005 не зависит от MCU. J1.3 остаётся в `unconnected-(J1-Pin_3-Pad3)`; внутреннюю цепь EN DevKit не меняли.

Последняя проверка KiCad 10.0.7 зафиксировала ERC без нарушений, 0 несоединённых элементов, 0 расхождений со схемой и 0 нарушений уровня ошибки. Общая DRC всё ещё завершается ненулевым кодом из-за 54 неподавленных предупреждений: 46 `track_dangling`, 3 `via_dangling` и 5 `copper_sliver`; пять унаследованных исключений не менялись. Все предусмотренные схемой электрические цепи соединены, но это не чистое прохождение DRC и не допуск к изготовлению.

Прошивки в рабочем дереве нет. Будущая прошивка должна только читать `/OCP_OK`, программно защёлкивать аварию, запрещать розжиг и самопуск и отключать FUEL/GLOW. Минимальная длительность импульса OCP, захват GPIO и общее время реакции требуют отдельной проверки; при неисправном двигателе нельзя обещать продувку.

Актуальные документы:

- [`docs/REQUIREMENTS_RU.md`](docs/REQUIREMENTS_RU.md) — требования;
- [`docs/UX_PANEL_REQUIREMENTS_RU.md`](docs/UX_PANEL_REQUIREMENTS_RU.md) — панель и управление;
- [`docs/LAYOUT_FREEDOM_RU.md`](docs/LAYOUT_FREEDOM_RU.md) — допустимая свобода компоновки;
- [`docs/CURRENT_STATE_RU.md`](docs/CURRENT_STATE_RU.md) — текущее состояние и ограничения;
- [`docs/research/YD_V14_ELECTRICAL_INTEGRATION_RU.md`](docs/research/YD_V14_ELECTRICAL_INTEGRATION_RU.md) — действующий контракт выводов DevKit;
- [`hardware/kicad/README.md`](hardware/kicad/README.md) — открытие проекта и локальные зависимости.

Ревизионные копии, отчёты проверок, рендеры, снимки и архивы не хранятся рядом с исходниками. Единственный путь восстановления прежних состояний — обычная история Git.
