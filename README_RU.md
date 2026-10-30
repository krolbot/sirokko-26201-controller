# Sirokko 262.01 — контроллер автоматики

> **DRAFT / NOT FOR ORDER / NO POWER / NO FLASH / NO FIRE.** Плата не закончена, не прошла производственную и физическую приёмку и не предназначена для заказа, подачи питания, прошивки или работы с горелкой.

В рабочем дереве оставлен один актуальный проект KiCad:

- проект: [`hardware/kicad/Sirokko26201_control_motor_candidate.kicad_pro`](hardware/kicad/Sirokko26201_control_motor_candidate.kicad_pro);
- схема: [`hardware/kicad/Sirokko26201_control_motor_candidate.kicad_sch`](hardware/kicad/Sirokko26201_control_motor_candidate.kicad_sch);
- плата: [`hardware/kicad/Sirokko26201_control_motor_candidate.kicad_pcb`](hardware/kicad/Sirokko26201_control_motor_candidate.kicad_pcb);
- SHA-256 текущей PCB: `0f29b0eb52f23cd80000c642781b561ff389ced245c62f7621cfe1a4008e463f`.

Текущая PCB содержит 144 посадочных места, 115 цепей, 1535 сегментов, 444 переходных отверстия и 35 зон. `GPIO18 = J1.11 = /OCP_OK`: цифровой вход Hi-Z, где LOW означает аппаратное событие токовой отсечки. MCU не должен переводить его в выходной режим или включать внутреннюю подтяжку вниз. Внешняя подтяжка R9008 к +3V3 уже есть; вторую не добавлять. Аппаратная отсечка через U9003 и U9005 не зависит от MCU. J1.3 остаётся в `unconnected-(J1-Pin_3-Pad3)`; внутреннюю цепь EN DevKit не меняли.

Принятая проверка KiCad 10.0.7 зафиксировала ERC без нарушений и честный общий FAIL DRC: 58 предупреждений и 17 незамкнутых соединений. Изменение OCP не добавило коротких замыканий, нарушений зазоров или разрывов. Это состояние незавершённой платы, а не допуск к изготовлению.

Прошивки в рабочем дереве нет. Будущая прошивка должна только читать `/OCP_OK`, программно защёлкивать аварию, запрещать розжиг и самопуск и отключать FUEL/GLOW. Минимальная длительность импульса OCP, захват GPIO и общее время реакции требуют отдельной проверки; при неисправном двигателе нельзя обещать продувку.

Актуальные документы:

- [`docs/REQUIREMENTS_RU.md`](docs/REQUIREMENTS_RU.md) — требования;
- [`docs/UX_PANEL_REQUIREMENTS_RU.md`](docs/UX_PANEL_REQUIREMENTS_RU.md) — панель и управление;
- [`docs/LAYOUT_FREEDOM_RU.md`](docs/LAYOUT_FREEDOM_RU.md) — допустимая свобода компоновки;
- [`docs/CURRENT_STATE_RU.md`](docs/CURRENT_STATE_RU.md) — текущее состояние и ограничения;
- [`docs/research/YD_V14_ELECTRICAL_INTEGRATION_RU.md`](docs/research/YD_V14_ELECTRICAL_INTEGRATION_RU.md) — действующий контракт выводов DevKit;
- [`hardware/kicad/README.md`](hardware/kicad/README.md) — открытие проекта и локальные зависимости.

Ревизионные копии, отчёты проверок, рендеры, снимки и архивы не хранятся рядом с исходниками. Единственный путь восстановления прежних состояний — обычная история Git.
