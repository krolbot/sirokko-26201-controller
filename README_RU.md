# Sirokko 262.01 — контроллер автоматики

> **DRAFT / REVIEW REQUIRED / NOT FOR ORDER / NO POWER / NO FLASH / NO FIRE; firmware HOLD.** Это не производственный выпуск и не доказательство работоспособности устройства. Плату нельзя заказывать, включать, прошивать или подключать к горелке.

В репозитории оставлен один канонический проект KiCad:

- проект: [`hardware/kicad/Sirokko26201_control_motor_candidate.kicad_pro`](hardware/kicad/Sirokko26201_control_motor_candidate.kicad_pro);
- корневая схема: [`hardware/kicad/Sirokko26201_control_motor_candidate.kicad_sch`](hardware/kicad/Sirokko26201_control_motor_candidate.kicad_sch);
- PCB: [`hardware/kicad/Sirokko26201_control_motor_candidate.kicad_pcb`](hardware/kicad/Sirokko26201_control_motor_candidate.kicad_pcb);
- SHA-256 PCB: `ede60af2ec3cc225c12836b2bf58f1e7a909507fb97195a61841afc22681ad20`;
- контур платы: 305 × 170 мм.

Колодки съёмного ESP32 DevKit перенесены: J1=(100,45; 100,9) мм, J3=(100,45; 75,5) мм, обе 90° на B.Cu. Текущий состав: 70 посадочных мест, 287 площадок, 615 сегментов, 140 переходов и 27 зон. Нативный DRC KiCad 10.0.8: 0 нарушений и 0 незамкнутых соединений; ERC корневой схемы: 0 нарушений. Отдельный полный DRC с перезаливкой зон, всеми ошибками дорожек и сверкой со схемой завершился с кодом 5 только из-за двух известных отсутствующих на PCB элементов R108/C109; `ignored_checks` пуст, SHA PCB не изменился. Нативная проверка связности сохранила сети и идентичность всех 287 площадок и все 1600 физических пар. Это только проверки CAD.

Актуальные изображения из этой PCB:

- [вид спереди](docs/board-front.png);
- [вид сзади](docs/board-back.png), зеркально со стороны обратного слоя;
- [наклонный 3D-вид](docs/board-3d-oblique.png);
- [3D-вид снизу](docs/board-3d-bottom.png).

Ограниченный отчёт о переносе: [`docs/research/ESP_RELOCATION_ACCEPTANCE_RU.md`](docs/research/ESP_RELOCATION_ACCEPTANCE_RU.md). Текущий полный отчёт DRC: [`docs/research/ESP_RELOCATION_FULL_DRC.json`](docs/research/ESP_RELOCATION_FULL_DRC.json). Действующие требования: [`docs/REQUIREMENTS_RU.md`](docs/REQUIREMENTS_RU.md). Текущее состояние и ограничения: [`docs/CURRENT_STATE_RU.md`](docs/CURRENT_STATE_RU.md). Производственного комплекта и выпуска прошивки нет. Старые проекты, кандидаты, ревизионные копии, CAM-архивы и устаревшие экспорты платы в рабочем дереве не хранятся; восстановление прежних состояний — через историю Git.
