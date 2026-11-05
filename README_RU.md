# Sirokko 262.01 — контроллер автоматики

> **DRAFT / REVIEW REQUIRED / NOT FOR ORDER / NO POWER / NO PHYSICAL FLASH / NO FIRE.** Это не производственный выпуск и не доказательство работоспособности устройства. Плату нельзя заказывать, включать, физически прошивать или подключать к горелке. Существующий программный DRAFT сохраняется для проверки, но дальнейшая разработка прошивки, расширение тестов и target-сборки поставлены на HOLD до совместного согласования с Никитой функций, меню и общей логики.

В репозитории оставлен один канонический проект KiCad:

- проект: [`hardware/kicad/Sirokko26201_control_motor_candidate.kicad_pro`](hardware/kicad/Sirokko26201_control_motor_candidate.kicad_pro);
- корневая схема: [`hardware/kicad/Sirokko26201_control_motor_candidate.kicad_sch`](hardware/kicad/Sirokko26201_control_motor_candidate.kicad_sch), SHA256 `10234cffe31a597494a45f9b3eb786bf3658f1df8872bafbda235fc0618970fd`;
- PCB: [`hardware/kicad/Sirokko26201_control_motor_candidate.kicad_pcb`](hardware/kicad/Sirokko26201_control_motor_candidate.kicad_pcb), SHA256 `3c414eb66da7ff7c6b44399a5515e97417703ccb50feea8ab9f17e66db17ab7f`;
- контур платы: 305 × 170 мм.

Все 73 посадочных места находятся на F.Cu. Плата содержит 303 площадки, 753 сегмента, 172 перехода и 27 зон. Готовая TFT2.4+EC11 подключается через J32; отдельной собственной PCB панели нет. [Распиновка](docs/TFT24_INTERFACE_RU.md).

KiCad10.0.8: rootERC0 при четырёх сохранённых ignored checks; nativeDRC0/opens0, DRC ignored=[]. Отдельно сопоставлены252электрическихpin-net с XML,308отверстий и60позиций установки. [Текущие проверки](manufacturing/current/checks/verification.json) не являются разрешением производства или питания. Прежние исследовательские отчёты с иными SHA относятся к истории, не к новой ревизии.

F1 — Littelfuse `0997015.WXN`, 15 А / 58 В DC, в неизменном держателе Keystone 3568. Назначенная оболочка интерфейсов: свеча 7 А непрерывно / 10 А не более 1 с; муфта 2 А / 3 А не более 1 с; общий вход J13/F1 10 А / 13 А не более 1 с; воздух у F1 не выше 80 °C. Предварительный непрерывный бюджет 5 В — 0,70 А. [Инженерное основание](docs/research/SIROKKO_POWER_AND_LOAD_ENVELOPE_RU.md) не заменяет физическую квалификацию.

Актуальные изображения:

- [сборочный вид спереди](manufacturing/current/renders/assembly-front.png);
- [сборочный вид сзади](manufacturing/current/renders/assembly-back.png);
- [3D-вид сверху](manufacturing/current/renders/board-3d-top.png);
- [3D-вид снизу](manufacturing/current/renders/board-3d-bottom.png);
- [CAM Gerber/Excellon](manufacturing/current/cam.zip);
- [карта внешних подключений](docs/research/CONTROL_CONNECTIONS_CURRENT_RU.md) и [изображение контактов](docs/research/CONTROL_CONNECTIONS_CURRENT.png).

Проверочный CAM/BOM пересобран в [`manufacturing/current/`](manufacturing/current/) из этой же пары SCH/PCB и остаётся **NOT FOR ORDER**. Действующие требования: [`docs/REQUIREMENTS_RU.md`](docs/REQUIREMENTS_RU.md), [согласованное управление отдельной панелью](docs/PANEL_CONTROL_RU.md), [текущее состояние](docs/CURRENT_STATE_RU.md), [требования изготовления](docs/research/FABRICATION_REQUIREMENTS_CURRENT_RU.md). Пуск, DC-bias, горячая SOA, импульсная координация и физические испытания не квалифицированы; восстановление прежних состояний — через историю Git.

Механическая ведомость [`docs/BOM_MECHANICAL_INTERFACES_DRAFT.csv`](docs/BOM_MECHANICAL_INTERFACES_DRAFT.csv) использует нативные базы Edge.Cuts `left=-5.000` и `top=10.000` мм. Все 13 производных `Y_from_top_mm` проверены правилом `Y_mm-10.000`; машинный отчёт — [`docs/research/CAM_DRILL_GEOMETRY_FINAL.json`](docs/research/CAM_DRILL_GEOMETRY_FINAL.json). CAD при исправлении ведомости не менялся.

## Программный DRAFT

Единый текущий исходный набор состоит из аппаратного проекта выше и каталога [`firmware/`](firmware/) в том же Git-дереве. Существующий DRAFT для YD-ESP32-S3 N16R8 V1.4 поднимает собственную AP и браузерную панель, содержит один автомат, два MAX31856, DS18B20, команды двух реле и HC-160A S2. Огневой профиль намеренно не квалифицирован, поэтому топливо программно заблокировано. Аппаратное подключение покупной TFT2.4+EC11 реализовано, но прошивка этой панели не менялась. [Согласованные жесты](docs/PANEL_CONTROL_RU.md); AP/браузер сохраняются, дальнейшая разработка прошивки — HOLD. Существующий состав и ограничения DRAFT: [`firmware/README_RU.md`](firmware/README_RU.md).
