# Sirokko 262.01 — контроллер автоматики

> **DRAFT / NOT FOR ORDER / NO POWER / NO FLASH / NO FIRE.** Плата не закончена, не имеет производственного допуска и не прошла физическую приёмку. Её нельзя заказывать, включать, прошивать или подключать к горелке. Сохранённые PCB и производственный комплект относятся к предшествующей дискретной ревизии MAX31856, а не к окончательной выбранной конструкции.

В рабочем дереве оставлен один опубликованный снимок проекта KiCad:

- проект: [`hardware/kicad/Sirokko26201_control_motor_candidate.kicad_pro`](hardware/kicad/Sirokko26201_control_motor_candidate.kicad_pro);
- схема: [`hardware/kicad/Sirokko26201_control_motor_candidate.kicad_sch`](hardware/kicad/Sirokko26201_control_motor_candidate.kicad_sch), SHA-256 `93e7b1063e20527aa473c37a7c1c3760ab218979f3829dc23b469994a237ae1c`;
- PCB: [`hardware/kicad/Sirokko26201_control_motor_candidate.kicad_pcb`](hardware/kicad/Sirokko26201_control_motor_candidate.kicad_pcb), SHA-256 `4362789e7bde2e35e92555165d99881d5b8c3cc20fbc2b3b6068f5cde9a44623`;
- дочерний лист питания: [`hardware/kicad/logic_power_24_5_DRAFT.kicad_sch`](hardware/kicad/logic_power_24_5_DRAFT.kicad_sch), SHA-256 `36248b3d5b1e4c93b9551ad1ab19b3abd59f357226a311998b310b7e8ac1514d`;
- дочерний лист защиты: [`hardware/kicad/MinimalSafety.kicad_sch`](hardware/kicad/MinimalSafety.kicad_sch), SHA-256 `6c47a85e123673808ccee73e2dfc46d868c64a6692af4cd6686a6cb65edc0d0d`.

Последнее прямое решение пользователя: применить два съёмных синих модуля MAX31856 по <https://ozon.ru/t/3QtIbaX> вместо собственных U9/U10 с обвязкой. Это не MAX31865. Модульная конструкция ещё не внесена в CAD: на Sirokko нужны ответные колодки и крепление, общая SPI с отдельными CS и DRDY; вывод `3Vo` модулей не соединять с питанием ESP. Геометрию, распиновку и питание точного исполнения должен проверить инженер перед следующей ревизией схемы/PCB/BOM; размеры из Adafruit или фотографий не переносить.

Текущая ревизия двухрелейная: K1 управляет свечой, K2 — муфтой. K3 и R703 удалены; K2.87 напрямую соединён с J28.1. U11.11 заземлён, U11.6 и GPIO21 — NC; GPIO18 остаётся NC. Подробное основание и пределы проверки: [`hardware/kicad/SIROKKO_TWO_RELAYS_CHANGE_RU.md`](hardware/kicad/SIROKKO_TWO_RELAYS_CHANGE_RU.md).

Текущий C104 — Murata `GRM1885C1H222JA01D`, 2,2 нФ, 50 В, C0G, ±5%. Основание выбора и пределы расчёта: [`docs/research/C104_BOOTSTRAP_REVIEW_RU.md`](docs/research/C104_BOOTSTRAP_REVIEW_RU.md). Это не физическая квалификация и не производственный допуск.

Экран, местная кнопка ПУСК/СТОП, энкодер и отдельная RESET отсутствуют. Управление, уставки и диагностика требуются через программный интерфейс на самом ESP32: собственная точка доступа и браузерная панель, без отдельного сервера и без MQTT-only. Штатное питание ESP32 — от Sirokko; USB предназначен только для прошивки и обслуживания, а возможная подпитка периферии остаётся ограничением, не рабочим источником.

Свежие KiCad 10.0.7 проверки: ERC — 0 нарушений с четырьмя явно перечисленными стандартными исключениями; DRC — 0 нарушений, 0 обрывов, 0 расхождений PCB↔SCH, `ignored_checks=[]`. Плата содержит 86 посадочных мест, 338 площадок, 870 сегментов и 255 переходов. Это проверка CAD, не электрический, тепловой или производственный допуск.

В предшествующей дискретной ревизии двух каналов MAX31856 BIAS соединён с T−, а C9/C14 по 0,1 мкФ включены между T− и T+. Основание и проверка: [`docs/research/THERMOCOUPLE_BIAS_CORRECTION_RU.md`](docs/research/THERMOCOUPLE_BIAS_CORRECTION_RU.md). Контракт будущего опроса двух измерителей: [`docs/research/THERMOCOUPLE_ACQUISITION_CONTRACT_RU.md`](docs/research/THERMOCOUPLE_ACQUISITION_CONTRACT_RU.md). Эти материалы подтверждают дискретный снимок и логический интерфейс, но не принятую модульную конструкцию. Локальный кадр разводки — только пояснительный фрагмент, не полный вид платы и не доказательство DRC или электрической корректности.

Актуальные материалы:

- [`hardware/kicad/ELECTRICAL_INTERFACE_RU.md`](hardware/kicad/ELECTRICAL_INTERFACE_RU.md) — действующая карта GPIO, клемм и релейной логики;
- [`docs/research/C104_BOOTSTRAP_REVIEW_RU.md`](docs/research/C104_BOOTSTRAP_REVIEW_RU.md) — принятая C0G-позиция C104 и ограничения её проверки;
- [`docs/research/THERMOCOUPLE_BIAS_CORRECTION_RU.md`](docs/research/THERMOCOUPLE_BIAS_CORRECTION_RU.md) — принятая коррекция BIAS и дифференциальных C9/C14;
- [`docs/research/THERMOCOUPLE_ACQUISITION_CONTRACT_RU.md`](docs/research/THERMOCOUPLE_ACQUISITION_CONTRACT_RU.md) — задание для будущей прошивки; не реализация и не допуск горения;
- [`docs/research/RELAY_DG85BM_COIL_ENVELOPE_REVIEW_RU.md`](docs/research/RELAY_DG85BM_COIL_ENVELOPE_REVIEW_RU.md) и [`docs/research/RELAY_DG85BM_COIL_ENVELOPE_REVIEW.json`](docs/research/RELAY_DG85BM_COIL_ENVELOPE_REVIEW.json) — пересчёт двух реле, драйвера и горячего втягивания;
- [`docs/REQUIREMENTS_RU.md`](docs/REQUIREMENTS_RU.md) — требования и незакрытые условия;
- [`docs/board-front.png`](docs/board-front.png) и [`docs/board-back.png`](docs/board-back.png) — текущие обзорные виды;
- [`manufacturing/DRAFT_NOT_FOR_ORDER/`](manufacturing/DRAFT_NOT_FOR_ORDER/) — пересобранные Gerber, Excellon, placement, BOM и сборочные PDF предшествующей дискретной PCB; это не целевая модульная ревизия;
- [`manufacturing/DRAFT_NOT_FOR_ORDER/SHA256SUMS`](manufacturing/DRAFT_NOT_FOR_ORDER/SHA256SUMS) — контрольные суммы комплекта.

Согласованной прошивки нет. Электрическая и тепловая квалификация, HC160, силовые режимы и физические испытания не закрыты. Ревизионные копии рядом с исходниками не хранятся; история — в Git.
