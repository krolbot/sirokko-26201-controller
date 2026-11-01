# Sirokko 262.01 — текущее состояние

Статус: **DRAFT / REVIEW REQUIRED / NOT FOR ORDER / NO POWER / NO FLASH / NO FIRE; firmware HOLD**. `production_release=false`.

## Канонический проект

Единственный действующий CAD-путь — [`../hardware/kicad/`](../hardware/kicad/):

- `.kicad_pro`: `11029d86464484cea63d73aa802bad95c1a6805aee11e841fc78322e9d2bc0d6`;
- root SCH: `6c62c1d9fc13e621cfff8c6be2f9548082b5b4e7aaf046d7d7f454c1077fc76a`;
- PCB: `ede60af2ec3cc225c12836b2bf58f1e7a909507fb97195a61841afc22681ad20`;
- `Sirokko_Input_Protection_WIP.kicad_sch`: `334a081f26003351fcd2a1ead5d2d6be32adb3dded125e289fbb2d50c8b3cfde`;
- `logic_power_24_5_DRAFT.kicad_sch`: `36248b3d5b1e4c93b9551ad1ab19b3abd59f357226a311998b310b7e8ac1514d`;
- `fp-lib-table`: `8e9cf2caf9090ad7d5e78625f981dc0fe8f22895f48515eb55261a27f1c247ac`.

Контур PCB: 305 × 170 мм. Колодки съёмного ESP32 DevKit перенесены: J1=(100,45; 100,9) мм, J3=(100,45; 75,5) мм, обе 90° на B.Cu. Схема, прошивка, назначения GPIO и силовая логика этой операцией не менялись.

## Свежая проверка

KiCad 10.0.8:

- нативный DRC после записи: 0 нарушений, 0 незамкнутых соединений;
- ERC корневой схемы: 0 нарушений;
- отдельный `pcb drc --all-track-errors --schematic-parity --severity-all --exit-code-violations --refill-zones` без `--save-board`: код 5, `violations=[]`, `unconnected_items=[]`, `ignored_checks=[]`, две известные записи `schematic_parity` — отсутствующие на PCB R108/C109;
- SHA PCB до и после отдельного запуска одинаков: `ede60af2ec3cc225c12836b2bf58f1e7a909507fb97195a61841afc22681ad20`;
- состав: 70 посадочных мест, 287 площадок, 615 сегментов, 140 переходов, 27 зон;
- нативная проверка связности: сети и идентичность всех 287 площадок сохранены; набор 1600 физических пар совпадает с предыдущим снимком, 0 потерянных и 0 добавленных;
- 670 защищённых прежних дорожек и переходов сохранили геометрию, слой и сеть.

Полный отчёт: [ESP_RELOCATION_FULL_DRC.json](research/ESP_RELOCATION_FULL_DRC.json). Ограниченная приёмка переноса: [ESP_RELOCATION_ACCEPTANCE_RU.md](research/ESP_RELOCATION_ACCEPTANCE_RU.md).

Изображения: [спереди](board-front.png), [сзади](board-back.png), [наклонный 3D-вид](board-3d-oblique.png), [3D-вид снизу](board-3d-bottom.png). Обратный плоский вид зеркальный. В 3D показаны колодки, но не тела DevKit и готовых модулей MAX31856; изображения не доказывают собранный габарит.

## Производственные материалы

Актуального производственного комплекта нет. Старые Gerber, Excellon, positions, STEP, PDF/PNG/SVG и ревизионные ZIP удалены из текущего дерева. До отдельной инженерной приёмки новые производственные копии не выпускаются. Предыдущие состояния остаются в истории Git.

## Незавершённое

- электрическая, тепловая и механическая квалификация;
- физические испытания;
- окончательная приёмка всей компоновки и платы;
- прошивка и браузерный интерфейс;
- производственный выпуск.

Питание, прошивка, двигатель, реле и огневые испытания не выполнялись и не разрешены. Действующие требования и ограничения: [REQUIREMENTS_RU.md](REQUIREMENTS_RU.md).
