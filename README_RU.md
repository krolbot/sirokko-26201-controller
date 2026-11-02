# Sirokko 262.01 — контроллер автоматики

> **DRAFT / REVIEW REQUIRED / NOT FOR ORDER / NO POWER / NO FLASH / NO FIRE; firmware HOLD.** Это не производственный выпуск и не доказательство работоспособности устройства. Плату нельзя заказывать, включать, прошивать или подключать к горелке.

В репозитории оставлен один канонический проект KiCad:

- проект: [`hardware/kicad/Sirokko26201_control_motor_candidate.kicad_pro`](hardware/kicad/Sirokko26201_control_motor_candidate.kicad_pro);
- корневая схема: [`hardware/kicad/Sirokko26201_control_motor_candidate.kicad_sch`](hardware/kicad/Sirokko26201_control_motor_candidate.kicad_sch);
- PCB: [`hardware/kicad/Sirokko26201_control_motor_candidate.kicad_pcb`](hardware/kicad/Sirokko26201_control_motor_candidate.kicad_pcb);
- SHA-256 PCB: `1b7a0936d9b96aaec3050cd86b12ce0b6929f403520aee953a22be1cb03e8a96`;
- контур платы: 305 × 170 мм.

Все 70 посадочных мест находятся на F.Cu. Колодки съёмного ESP32 DevKit: J1=(100,45; 75,5) мм и J3=(100,45; 100,9) мм, обе −90°. Текущий состав: 70 посадочных мест, 287 площадок, 633 сегмента, 145 переходов и 27 зон. У 75 безопасных переходов 0,60/0,30 мм площадки увеличены до 0,80/0,30 мм; 28 конфликтующих оставлены прежними. UUID, отверстия, координаты, цепи, слои и вся геометрия дорожек сохранены. Native DRC KiCad 10.0.8 после перезаливки: 0 геометрических нарушений и 0 незамкнутых соединений. Отдельная сверка со схемой дала код 5 только из-за прежних R108/C109. [Полная приёмка изменения](docs/research/VIA_RING_NATIVE_ACCEPTANCE_CURRENT_RU.md).

Эта ревизия остаётся промежуточной WIP и не разрешает заказ или физическое включение. Существующий `manufacturing/current/` собран из прежней PCB `9e10942d…`, поэтому после изменения переходов явно устарел и должен быть пересобран только после закрытия парности SCH/PCB.

Актуальные изображения из этой PCB:

- [вид спереди](docs/board-front.png);
- [вид сзади](docs/board-back.png), зеркально со стороны обратного слоя;
- [наклонный 3D-вид](docs/board-3d-oblique.png);
- [3D-вид снизу](docs/board-3d-bottom.png).

Действующие требования: [`docs/REQUIREMENTS_RU.md`](docs/REQUIREMENTS_RU.md). Текущее состояние и ограничения: [`docs/CURRENT_STATE_RU.md`](docs/CURRENT_STATE_RU.md). Актуальные требования обычного хоббийного изготовления: [`docs/research/FABRICATION_REQUIREMENTS_CURRENT_RU.md`](docs/research/FABRICATION_REQUIREMENTS_CURRENT_RU.md). Выпуска прошивки и актуального производственного комплекта нет; восстановление прежних состояний — через историю Git.
