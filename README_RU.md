# Sirokko 262.01 — контроллер автоматики

> **DRAFT / REVIEW REQUIRED / NOT FOR ORDER / NO POWER / NO FLASH / NO FIRE; firmware HOLD.** Это не производственный выпуск и не доказательство работоспособности устройства. Плату нельзя заказывать, включать, прошивать или подключать к горелке.

В репозитории оставлен один канонический проект KiCad:

- проект: [`hardware/kicad/Sirokko26201_control_motor_candidate.kicad_pro`](hardware/kicad/Sirokko26201_control_motor_candidate.kicad_pro);
- корневая схема: [`hardware/kicad/Sirokko26201_control_motor_candidate.kicad_sch`](hardware/kicad/Sirokko26201_control_motor_candidate.kicad_sch), SHA256 `c8c5bd6e2bc0cb61428037c93b54b6c5d26ea97d9e6529f3facb100935ee3b3b`;
- PCB: [`hardware/kicad/Sirokko26201_control_motor_candidate.kicad_pcb`](hardware/kicad/Sirokko26201_control_motor_candidate.kicad_pcb), SHA256 `46b84ef09d00a724962753ac0e83492b63a43177d69b33a519c890d5be9a0820`;
- контур платы: 305 × 170 мм.

Все 72 посадочных места находятся на F.Cu. Плата содержит 291 площадку, 639 сегментов, 147 переходов, 78 сетей и 27 зон. R108/C109 интегрированы рядом с U13; C24 заменён на Samsung `CL21B224KCFSFNE` без изменения посадки и сетей. Все прежние 70 посадочных мест, 633 сегмента, 145 переходов и площадки сохранены; добавлены только R108/C109, шесть сегментов и два перехода.

KiCad 10.0.8: root ERC 0, native DRC 0 нарушений/0 несоединений, Pre-fab Gate пройден с реально включённой проверкой соответствия схеме и `ignored_checks=[]`. [Текущая проверка](docs/research/PCB_SCH_PARITY_CURRENT_RU.md) и [машинная сводка](docs/research/RC_INTEGRATION_CURRENT.json) не являются разрешением производства или питания.

Актуальные изображения:

- [вид спереди](docs/board-front.png);
- [вид сзади](docs/board-back.png), зеркально со стороны обратного слоя;
- [3D-вид сверху](docs/board-3d-oblique.png);
- [3D-вид снизу](docs/board-3d-bottom.png);
- [карта внешних подключений](docs/research/CONTROL_CONNECTIONS_CURRENT_RU.md) и [изображение контактов](docs/research/CONTROL_CONNECTIONS_CURRENT.png).

Проверочный CAM/BOM пересобран в [`manufacturing/current/`](manufacturing/current/) из этой же пары SCH/PCB и остаётся **NOT FOR ORDER**. Действующие требования: [`docs/REQUIREMENTS_RU.md`](docs/REQUIREMENTS_RU.md), [текущее состояние](docs/CURRENT_STATE_RU.md), [требования изготовления](docs/research/FABRICATION_REQUIREMENTS_CURRENT_RU.md). Пуск, DC-bias, горячая SOA, импульсная координация и физические испытания не квалифицированы; восстановление прежних состояний — через историю Git.
