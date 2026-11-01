# Sirokko 262.01 — контроллер автоматики

> **DRAFT / REVIEW REQUIRED / NOT FOR ORDER / NO POWER / NO FLASH / NO FIRE; firmware HOLD.** Это не производственный выпуск и не доказательство работоспособности устройства. Плату нельзя заказывать, включать, прошивать или подключать к горелке.

В репозитории оставлен один канонический проект KiCad:

- проект: [`hardware/kicad/Sirokko26201_control_motor_candidate.kicad_pro`](hardware/kicad/Sirokko26201_control_motor_candidate.kicad_pro);
- корневая схема: [`hardware/kicad/Sirokko26201_control_motor_candidate.kicad_sch`](hardware/kicad/Sirokko26201_control_motor_candidate.kicad_sch);
- PCB: [`hardware/kicad/Sirokko26201_control_motor_candidate.kicad_pcb`](hardware/kicad/Sirokko26201_control_motor_candidate.kicad_pcb);
- SHA-256 PCB: `6b468c5fa23f7dce844ba41a267a18a37fbd6e21685c135afdaadc7fb7f77b5e`;
- контур платы: 305 × 170 мм.

KiCad 10.0.8 заново залил 19 медных зон. Свежие проверки: DRC=0, ERC=0, незамкнутых соединений=0. Нативное чтение до и после заливки подтвердило неизменность 70 посадочных мест, 287 площадок, 617 сегментов, 139 переходов, 27 зон и всех 1600 физических пар площадок. `validate_board` не нашёл нарушений размещения или выхода за край. Это только проверка CAD.

Актуальные изображения из канонической PCB:

- [вид спереди](docs/board-front.png);
- [вид сзади](docs/board-back.png);
- [наклонный 3D-вид](docs/board-3d-oblique.png).

Действующие требования: [`docs/REQUIREMENTS_RU.md`](docs/REQUIREMENTS_RU.md). Текущее состояние и ограничения: [`docs/CURRENT_STATE_RU.md`](docs/CURRENT_STATE_RU.md). Старые проекты, кандидаты, ревизионные копии, CAM-архивы и устаревшие экспорты платы в рабочем дереве не хранятся; восстановление прежних состояний — через историю Git.
