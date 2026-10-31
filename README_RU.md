# Sirokko 262.01 — контроллер автоматики

> **DRAFT / NOT FOR ORDER / NO POWER / NO FLASH / NO FIRE.** Плата не закончена, не имеет производственного допуска и не прошла физическую приёмку. Её нельзя заказывать, включать, прошивать или подключать к горелке.

В рабочем дереве оставлен один актуальный проект KiCad:

- проект: [`hardware/kicad/Sirokko26201_control_motor_candidate.kicad_pro`](hardware/kicad/Sirokko26201_control_motor_candidate.kicad_pro);
- схема: [`hardware/kicad/Sirokko26201_control_motor_candidate.kicad_sch`](hardware/kicad/Sirokko26201_control_motor_candidate.kicad_sch), SHA-256 `8857489e2cb666875fa123cc92b904645efd90dff04d8d8b168e6403f9691649`;
- PCB: [`hardware/kicad/Sirokko26201_control_motor_candidate.kicad_pcb`](hardware/kicad/Sirokko26201_control_motor_candidate.kicad_pcb), SHA-256 `041a1be8a39f86fb170d85339437b5d042ab97284b2194ba92d137a9dec39b4e`;
- дочерний лист защиты: [`hardware/kicad/MinimalSafety.kicad_sch`](hardware/kicad/MinimalSafety.kicad_sch), SHA-256 `6c47a85e123673808ccee73e2dfc46d868c64a6692af4cd6686a6cb65edc0d0d`.

Текущая ревизия двухрелейная: K1 управляет свечой, K2 — муфтой. K3 и R703 удалены; K2.87 напрямую соединён с J28.1. U11.11 заземлён, U11.6 и GPIO21 — NC; GPIO18 остаётся NC. Подробное основание и пределы проверки: [`hardware/kicad/SIROKKO_TWO_RELAYS_CHANGE_RU.md`](hardware/kicad/SIROKKO_TWO_RELAYS_CHANGE_RU.md).

Экран, местная кнопка ПУСК/СТОП, энкодер и отдельная RESET отсутствуют. Управление, уставки и диагностика требуются через программный интерфейс на самом ESP32: собственная точка доступа и браузерная панель, без отдельного сервера и без MQTT-only. Штатное питание ESP32 — от Sirokko; USB предназначен только для прошивки и обслуживания, а возможная подпитка периферии остаётся ограничением, не рабочим источником.

Свежие KiCad 10.0.7 проверки: ERC — 0 нарушений с четырьмя явно перечисленными стандартными исключениями; DRC — 0 нарушений, 0 обрывов, 0 расхождений PCB↔SCH, `ignored_checks=[]`. Плата содержит 86 посадочных мест, 338 площадок, 861 сегмент и 253 перехода. Это проверка CAD, не электрический, тепловой или производственный допуск.

Актуальные материалы:

- [`hardware/kicad/ELECTRICAL_INTERFACE_RU.md`](hardware/kicad/ELECTRICAL_INTERFACE_RU.md) — действующая карта GPIO, клемм и релейной логики;
- [`docs/REQUIREMENTS_RU.md`](docs/REQUIREMENTS_RU.md) — требования и незакрытые условия;
- [`docs/board-front.png`](docs/board-front.png) и [`docs/board-back.png`](docs/board-back.png) — текущие обзорные виды;
- [`manufacturing/DRAFT_NOT_FOR_ORDER/`](manufacturing/DRAFT_NOT_FOR_ORDER/) — пересобранные Gerber, Excellon, placement, BOM и сборочные PDF текущей PCB;
- [`manufacturing/DRAFT_NOT_FOR_ORDER/SHA256SUMS`](manufacturing/DRAFT_NOT_FOR_ORDER/SHA256SUMS) — контрольные суммы комплекта.

Согласованной прошивки нет. Электрическая и тепловая квалификация, HC160, силовые режимы и физические испытания не закрыты. Ревизионные копии рядом с исходниками не хранятся; история — в Git.
