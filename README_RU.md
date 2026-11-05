# Sirokko 262.01 — контроллер автоматики

> **Аппаратная ревизия v6: цифровой комплект изготовления с KEY0.** Можно передавать на изготовление голой платы; монтаж компонентов — отдельно. [Актуальный комплект и условия](manufacturing/current/README_RU.md). `production_release=false`, `physical_tests=false`: физическая квалификация и допуск эксплуатации не заявляются. Прошивка остаётся на HOLD до согласования функций, меню и общей логики; неопубликованный черновик KEY0 в этот коммит не входит.

В репозитории оставлен один канонический проект KiCad:

- проект: [`hardware/kicad/Sirokko26201_control_motor_candidate.kicad_pro`](hardware/kicad/Sirokko26201_control_motor_candidate.kicad_pro);
- корневая схема: [`hardware/kicad/Sirokko26201_control_motor_candidate.kicad_sch`](hardware/kicad/Sirokko26201_control_motor_candidate.kicad_sch), SHA256 `0b85bf2f4921b25a1c3126348eb176a88c7e3b29ca474c2d7229c4367e0a5cf5`;
- PCB: [`hardware/kicad/Sirokko26201_control_motor_candidate.kicad_pcb`](hardware/kicad/Sirokko26201_control_motor_candidate.kicad_pcb), SHA256 `88f2459caeddaece2577aa9685bec969e2997f6bbda6fc0931fae324b8b0ff33`;
- контур платы: 281 × 170 мм.

Все 73 посадочных места находятся на F.Cu. Плата содержит 303 площадки, 667 сегментов, 147 переходов и 25 зон. Готовая TFT2.4+EC11 подключается через J32; отдельной собственной PCB панели нет. [Распиновка](docs/TFT24_INTERFACE_RU.md).

KiCad10.0.8: rootERC0 при четырёх сохранённых ignored checks; nativeDRC0/opens0, DRC ignored=[]. Отдельно сопоставлены252электрическихpin-net с XML,283отверстий и60позиций установки. [Текущие проверки](manufacturing/current/checks/verification.json) подтверждают цифровую проверку, но не физическую работоспособность. Прежние исследовательские отчёты с иными SHA относятся к истории, не к новой ревизии.

F1 — Littelfuse `0997015.WXN`, 15 А / 58 В DC, в неизменном держателе Keystone 3568. Назначенная оболочка интерфейсов: свеча 7 А непрерывно / 10 А не более 1 с; муфта 2 А / 3 А не более 1 с; общий вход J13/F1 10 А / 13 А не более 1 с; воздух у F1 не выше 80 °C. Предварительный непрерывный бюджет 5 В — 0,70 А. [Инженерное основание](docs/research/SIROKKO_POWER_AND_LOAD_ENVELOPE_RU.md) не заменяет физическую квалификацию.

Актуальные изображения:

- [сборочный вид спереди](manufacturing/current/renders/assembly-front.png);
- [медь обратной стороны](manufacturing/current/renders/routing-B_Cu.png);
- [3D-вид сверху](manufacturing/current/renders/board-3d-top.png);
- [3D-вид снизу](manufacturing/current/renders/board-3d-bottom.png);
- [CAM Gerber/Excellon](manufacturing/current/cam.zip);
- [карта внешних подключений](docs/research/CONTROL_CONNECTIONS_CURRENT_RU.md) и [изображение контактов](docs/research/CONTROL_CONNECTIONS_CURRENT.png).

CAM/BOM пересобран в [`manufacturing/current/`](manufacturing/current/) из этой же пары SCH/PCB; манифест и независимый просмотр CAM относятся к окончательной ревизии KEY0. Для изготовления использовать [актуальный README](manufacturing/current/README_RU.md), а не исторические запреты старых ревизий. Действующие требования: [`docs/REQUIREMENTS_RU.md`](docs/REQUIREMENTS_RU.md), [согласованное управление отдельной панелью](docs/PANEL_CONTROL_RU.md), [текущее состояние](docs/CURRENT_STATE_RU.md), [требования изготовления](docs/research/FABRICATION_REQUIREMENTS_CURRENT_RU.md). Пуск, DC-bias, горячая SOA, импульсная координация и физические испытания не квалифицированы; восстановление прежних состояний — через историю Git.

Механическая ведомость [`docs/BOM_MECHANICAL_INTERFACES_DRAFT.csv`](docs/BOM_MECHANICAL_INTERFACES_DRAFT.csv) использует нативные базы Edge.Cuts `left=-5.000` и `top=10.000` мм. Все 13 производных `Y_from_top_mm` проверены правилом `Y_mm-10.000`; машинный отчёт — [`docs/research/CAM_DRILL_GEOMETRY_FINAL.json`](docs/research/CAM_DRILL_GEOMETRY_FINAL.json). CAD при исправлении ведомости не менялся.

## Программный DRAFT

Единый текущий исходный набор состоит из аппаратного проекта выше и каталога [`firmware/`](firmware/) в том же Git-дереве. Существующий DRAFT для YD-ESP32-S3 N16R8 V1.4 поднимает собственную AP и браузерную панель, содержит один автомат, два MAX31856, DS18B20, команды двух реле и HC-160A S2. Огневой профиль намеренно не квалифицирован, поэтому топливо программно заблокировано. KEY0 покупной TFT2.4+EC11 подключена через J32.12 к GPIO40/J3.8; назначение Пуск/останов перенесено с энкодера на KEY0. Времена нажатий и реализация прошивки не утверждены; локальный черновик не опубликован. Драйвер TFT, локальное меню и навигация энкодером этой доработкой не реализованы. [Согласованные жесты](docs/PANEL_CONTROL_RU.md); AP/браузер сохраняются, дальнейшая разработка прошивки — HOLD. Существующий состав и ограничения DRAFT: [`firmware/README_RU.md`](firmware/README_RU.md).
