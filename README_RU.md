# Sirokko 262.01 — контроллер автоматики

> **DRAFT / NOT FOR ORDER / NO POWER / NO FIRE.** Плата не закончена, не прошла производственную и физическую приёмку и не предназначена для заказа, подачи питания или работы с горелкой.

В рабочем дереве оставлен один актуальный полный проект KiCad:

- проект: [`hardware/kicad/Sirokko26201_control_motor_candidate.kicad_pro`](hardware/kicad/Sirokko26201_control_motor_candidate.kicad_pro);
- схема: [`hardware/kicad/Sirokko26201_control_motor_candidate.kicad_sch`](hardware/kicad/Sirokko26201_control_motor_candidate.kicad_sch);
- плата: [`hardware/kicad/Sirokko26201_control_motor_candidate.kicad_pcb`](hardware/kicad/Sirokko26201_control_motor_candidate.kicad_pcb);
- текущая PCB after63 corrected: `f0cbceafcff1f6845bc850b090935e0a5f719f1fe8f087e952177cfb92e8722f`.

После cleanup последовательно опубликованы after20, after30, after34, after42, after52 и after55. Текущая PCB сохраняет все 1776 объектов after55 по UUID, сети, слою и геометрии и добавляет ровно семь сегментов 0,25 мм и два перехода 0,6/0,3 мм. Отклонённая диагональ GND заменена двумя сегментами; новый `copper_sliver` исчез. Подробности: [`docs/CURRENT_STATE_RU.md`](docs/CURRENT_STATE_RU.md).

Отозванная условная модель крепежа голого TFT на стойках M2×12 удалена из проекта. Официальная номинальная STEP-модель Waveshare 18366 сохранена, но резьба, высота штатных опор, сопрягаемые детали и полный высотный размер не приняты.

Прошивки в рабочем дереве нет. Прежняя стендовая реализация была отвергнута как основа конечного изделия и сохранена в истории Git. Новую прошивку следует разрабатывать только после окончательной приёмки PCB и карты выводов. Драйвер ILI9341 отсутствует и не считается готовым.

Актуальные документы:

- [`docs/REQUIREMENTS_RU.md`](docs/REQUIREMENTS_RU.md) — требования;
- [`docs/UX_PANEL_REQUIREMENTS_RU.md`](docs/UX_PANEL_REQUIREMENTS_RU.md) — панель и управление;
- [`docs/LAYOUT_FREEDOM_RU.md`](docs/LAYOUT_FREEDOM_RU.md) — допустимая свобода компоновки;
- [`docs/CURRENT_STATE_RU.md`](docs/CURRENT_STATE_RU.md) — текущая ревизия и ограничения;
- [`hardware/kicad/README.md`](hardware/kicad/README.md) — открытие проекта и локальные зависимости;
- [`verification/current/README_RU.md`](verification/current/README_RU.md) — журнал свежих проверок этой ревизии.

Исторические CAD-копии, WIP-кандидаты, отменённые инструкции и старая прошивка удалены из текущего дерева; они остаются доступны через обычную историю Git.
