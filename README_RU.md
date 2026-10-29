# Sirokko 262.01 — контроллер автоматики

> **DRAFT / NOT FOR ORDER / NO POWER / NO FIRE.** Плата не закончена, не прошла производственную и физическую приёмку и не предназначена для заказа, подачи питания или работы с горелкой.

В рабочем дереве оставлен один актуальный полный проект KiCad:

- проект: [`hardware/kicad/Sirokko26201_control_motor_candidate.kicad_pro`](hardware/kicad/Sirokko26201_control_motor_candidate.kicad_pro);
- схема: [`hardware/kicad/Sirokko26201_control_motor_candidate.kicad_sch`](hardware/kicad/Sirokko26201_control_motor_candidate.kicad_sch);
- плата: [`hardware/kicad/Sirokko26201_control_motor_candidate.kicad_pcb`](hardware/kicad/Sirokko26201_control_motor_candidate.kicad_pcb);
- SHA-256 текущей PCB: `f0cbceafcff1f6845bc850b090935e0a5f719f1fe8f087e952177cfb92e8722f`.

Текущая PCB содержит 158 посадочных мест, 603 физические площадки, 1384 сегмента и 401 переходное отверстие. Последняя нативная проверка зафиксировала 83 DRC-нарушения и 80 незамкнутых соединений; ERC — 0 нарушений. Это состояние незавершённой платы, а не допуск к изготовлению.

Отозванная условная модель крепежа голого TFT на стойках M2×12 удалена. Официальная номинальная STEP-модель Waveshare 18366 сохранена, но резьба, высота штатных опор, сопрягаемые детали и полный высотный размер не приняты.

Прошивки в рабочем дереве нет. Прежняя стендовая реализация была отвергнута как основа конечного изделия и доступна только через историю Git. Новую прошивку следует разрабатывать после окончательной приёмки PCB и карты выводов; драйвер ILI9341 отсутствует.

Актуальные документы:

- [`docs/REQUIREMENTS_RU.md`](docs/REQUIREMENTS_RU.md) — требования;
- [`docs/UX_PANEL_REQUIREMENTS_RU.md`](docs/UX_PANEL_REQUIREMENTS_RU.md) — панель и управление;
- [`docs/LAYOUT_FREEDOM_RU.md`](docs/LAYOUT_FREEDOM_RU.md) — допустимая свобода компоновки;
- [`docs/CURRENT_STATE_RU.md`](docs/CURRENT_STATE_RU.md) — текущее состояние и ограничения;
- [`hardware/kicad/README.md`](hardware/kicad/README.md) — открытие проекта и локальные зависимости.

Ревизионные копии, отчёты проверок, рендеры, снимки и архивы не хранятся рядом с исходниками. Единственный путь восстановления прежних состояний — обычная история Git.
