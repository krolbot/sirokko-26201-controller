# Sirokko 262.01 — контроллер автоматики

> **DRAFT / REVIEW REQUIRED / NOT FOR ORDER / NO POWER / NO PHYSICAL FLASH / NO FIRE.** Это не производственный выпуск и не доказательство работоспособности устройства. Плату нельзя заказывать, включать, физически прошивать или подключать к горелке. Программный DRAFT и target-сборка разрешены только для разработки.

В репозитории оставлен один канонический проект KiCad:

- проект: [`hardware/kicad/Sirokko26201_control_motor_candidate.kicad_pro`](hardware/kicad/Sirokko26201_control_motor_candidate.kicad_pro);
- корневая схема: [`hardware/kicad/Sirokko26201_control_motor_candidate.kicad_sch`](hardware/kicad/Sirokko26201_control_motor_candidate.kicad_sch), SHA256 `0f62b97606d5220ebe1cb630b59fd2050777e6d9020e623b93a4c5518774f76e`;
- PCB: [`hardware/kicad/Sirokko26201_control_motor_candidate.kicad_pcb`](hardware/kicad/Sirokko26201_control_motor_candidate.kicad_pcb), SHA256 `57f4b9437de4176f61e99d71ef923711f1368ba13d1de3b72c6fd13d82a4d09b`;
- контур платы: 305 × 170 мм.

Все 72 посадочных места находятся на F.Cu. Плата содержит 291 площадку, 639 сегментов, 146 переходов, 78 сетей и 27 зон. В `/TC_SDI_MOSI` удалён один переход без увеличения длины сети; остальные проверенные кандидаты отклонены по DRC/связности. R108/C109 и Samsung `CL21B224KCFSFNE` сохранены.

KiCad 10.0.8: root ERC 0, native DRC 0 нарушений/0 несоединений, Pre-fab Gate пройден с реально включённой проверкой соответствия схеме и `ignored_checks=[]`. [Текущая проверка](docs/research/PCB_SCH_PARITY_CURRENT_RU.md) и [машинная сводка](docs/research/RC_INTEGRATION_CURRENT.json) не являются разрешением производства или питания.

F1 — Littelfuse `0997015.WXN`, 15 А / 58 В DC, в неизменном держателе Keystone 3568. Назначенная оболочка интерфейсов: свеча 7 А непрерывно / 10 А не более 1 с; муфта 2 А / 3 А не более 1 с; общий вход J13/F1 10 А / 13 А не более 1 с; воздух у F1 не выше 80 °C. Предварительный непрерывный бюджет 5 В — 0,70 А. [Инженерное основание](docs/research/SIROKKO_POWER_AND_LOAD_ENVELOPE_RU.md) не заменяет физическую квалификацию.

Актуальные изображения:

- [сборочный вид спереди](manufacturing/current/renders/assembly-front.png);
- [сборочный вид сзади](manufacturing/current/renders/assembly-back.png);
- [3D-вид сверху](manufacturing/current/renders/board-3d-top.png);
- [3D-вид снизу](manufacturing/current/renders/board-3d-bottom.png);
- [карта внешних подключений](docs/research/CONTROL_CONNECTIONS_CURRENT_RU.md) и [изображение контактов](docs/research/CONTROL_CONNECTIONS_CURRENT.png).

Проверочный CAM/BOM пересобран в [`manufacturing/current/`](manufacturing/current/) из этой же пары SCH/PCB и остаётся **NOT FOR ORDER**. Действующие требования: [`docs/REQUIREMENTS_RU.md`](docs/REQUIREMENTS_RU.md), [текущее состояние](docs/CURRENT_STATE_RU.md), [требования изготовления](docs/research/FABRICATION_REQUIREMENTS_CURRENT_RU.md). Пуск, DC-bias, горячая SOA, импульсная координация и физические испытания не квалифицированы; восстановление прежних состояний — через историю Git.

Механическая ведомость [`docs/BOM_MECHANICAL_INTERFACES_DRAFT.csv`](docs/BOM_MECHANICAL_INTERFACES_DRAFT.csv) использует нативные базы Edge.Cuts `left=-5.000` и `top=10.000` мм. Все 13 производных `Y_from_top_mm` проверены правилом `Y_mm-10.000`; машинный отчёт — [`docs/research/CAM_DRILL_GEOMETRY_FINAL.json`](docs/research/CAM_DRILL_GEOMETRY_FINAL.json). CAD при исправлении ведомости не менялся.

## Программный DRAFT

Единый текущий исходный набор состоит из аппаратного проекта выше и каталога [`firmware/`](firmware/) в том же Git-дереве. Прошивка для YD-ESP32-S3 N16R8 V1.4 поднимает собственную AP и браузерную панель, содержит один автомат, два MAX31856, DS18B20, команды двух реле и HC-160A S2. Огневой профиль намеренно не квалифицирован, поэтому топливо программно заблокировано. Сборка, состав бинарного комплекта, AP, пароль, ограничения и список не выполненных физических проверок: [`firmware/README_RU.md`](firmware/README_RU.md).
