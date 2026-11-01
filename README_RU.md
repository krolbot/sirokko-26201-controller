# Sirokko 262.01 — контроллер автоматики

> **DRAFT / NOT FOR ORDER / NO POWER / NO FLASH / NO FIRE; firmware HOLD.** Плата не закончена, не имеет производственного допуска и не прошла физическую приёмку. Её нельзя заказывать, включать, прошивать или подключать к горелке. Схема и PCB уже переведены на съёмные MAX31856 с назначенными колодками Samtec и проходят текущую проверку паритета CAD. Производственный комплект CAM и сборочные материалы остались от предшествующей дискретной ревизии и текущей PCB не соответствуют; устаревшие копии BOM из него удалены.

В рабочем дереве оставлен один намеренно незавершённый проект KiCad:

- проект: [`hardware/kicad/Sirokko26201_control_motor_candidate.kicad_pro`](hardware/kicad/Sirokko26201_control_motor_candidate.kicad_pro);
- схема: [`hardware/kicad/Sirokko26201_control_motor_candidate.kicad_sch`](hardware/kicad/Sirokko26201_control_motor_candidate.kicad_sch), SHA-256 `902d0681f072a57559e78aff44cfd5b877478634b8bae62d3e4e834592a3c979`;
- PCB: [`hardware/kicad/Sirokko26201_control_motor_candidate.kicad_pcb`](hardware/kicad/Sirokko26201_control_motor_candidate.kicad_pcb), SHA-256 `6042da2346f1ef873cda83086b57a3c0f69b981a6719fdd80eba219d4ae1b81a`;
- `fp-lib-table`: SHA-256 `8e9cf2caf9090ad7d5e78625f981dc0fe8f22895f48515eb55261a27f1c247ac`;
- дочерний лист питания: [`hardware/kicad/logic_power_24_5_DRAFT.kicad_sch`](hardware/kicad/logic_power_24_5_DRAFT.kicad_sch), SHA-256 `36248b3d5b1e4c93b9551ad1ab19b3abd59f357226a311998b310b7e8ac1514d`;
- дочерний лист защиты: [`hardware/kicad/MinimalSafety.kicad_sch`](hardware/kicad/MinimalSafety.kicad_sch), SHA-256 `3419aa24e854817fa59c91646ded28834a511ab17568cda566763b223c3f3ef0`.

Последнее прямое решение пользователя: применить два съёмных синих модуля MAX31856 по <https://ozon.ru/t/3QtIbaX> вместо собственных U9/U10 с обвязкой. Это не MAX31865. Пользователь принял официальную геометрию и распиновку Adafruit 3263 как документальную основу визуально идентичного исполнения. В схеме и PCB установлены J30/J31: общая SPI, отдельные CS/DRDY, FLT и `3Vo` не подключены, VIN получает +3V3. Обеим колодкам назначены MPN `SSW-109-01-T-S` и посадочное место `Connector_Sirokko_Module:Samtec_SSW-109-01-T-S_1x09_P2.54mm_D1.04mm`; все 18 сверлений имеют Ø1,04 мм. Модульная BOM опубликована только как черновик: конкретный крепёж, точные исполнения внешних изделий, закупочная готовность, полная объёмная механическая приёмка и актуальный CAM ещё не закончены.

Текущая ревизия двухрелейная: K1 управляет свечой, K2 — муфтой. K3 и R703 удалены; K2.87 напрямую соединён с J28.1. U11.11 заземлён, U11.6 и GPIO21 — NC; GPIO18 остаётся NC. Подробное основание и пределы проверки: [`hardware/kicad/SIROKKO_TWO_RELAYS_CHANGE_RU.md`](hardware/kicad/SIROKKO_TWO_RELAYS_CHANGE_RU.md).

Текущий C104 — Murata `GRM1885C1H222JA01D`, 2,2 нФ, 50 В, C0G, ±5%. Основание выбора и пределы расчёта: [`docs/research/C104_BOOTSTRAP_REVIEW_RU.md`](docs/research/C104_BOOTSTRAP_REVIEW_RU.md). Это не физическая квалификация и не производственный допуск.

Экран, местная кнопка ПУСК/СТОП, энкодер и отдельная RESET отсутствуют. Управление, уставки и диагностика требуются через программный интерфейс на самом ESP32: собственная точка доступа и браузерная панель, без отдельного сервера и без MQTT-only. Штатное питание ESP32 — от Sirokko; USB предназначен только для прошивки и обслуживания, а возможная подпитка периферии остаётся ограничением, не рабочим источником.

Первый принятый шаг общей перекомпоновки переносит J29 из `(324,00; 164,76)` в `(115,00; 130,00)` мм без изменения угла, нумерации и сетей. Заново проложены только `/FAN_REQUEST` и `/SPEED_CMD`; число переходов на всей плате уменьшено с 210 до 188. Контур и двухслойность не изменены, общая перекомпоновка не завершена.

Следующий принятый шаг переразводил только `/ROOM_DQ`: 15→3 перехода, длина сегментов 281,554→234,503 мм. Все изменения J29 сохранены; положение компонентов, схема, библиотеки и правила не менялись.

Следующий принятый шаг переразводил только сигналы управления реле `/FUEL_CMD` и `/GLOW_CMD`: число переходов уменьшено с 8→2 и 8→4, длина сегментов — с 187,338→182,535 мм и 227,975→180,974 мм соответственно. `/P24_PRESENT` полностью сохранена из предыдущей принятой PCB после отклонения неудачного варианта её новой трассировки.

Текущий принятый шаг переносит подтяжку R3 10 кОм сигнала `/TC_AIR_CS` из `(165;85)` мм в `(110;36,95)` мм к ESP32. Основной маршрут CS сохранён; ответвление теперь имеет 1 переход вместо 5 и длину сегментов 101,795 мм вместо 208,938 мм. На всей плате теперь 162 перехода — на 48 меньше исходной модульной базы; общая перекомпоновка ещё не завершена.

Свежие проверки KiCad 10.0.8: ERC=0, DRC=0, 0 обрывов и 0 расхождений PCB↔SCH. В DRC `ignored_checks=[]`; ERC сохраняет четыре явно названных штатных класса исключений, новых не добавлено. PCB содержит 78 посадочных мест, 308 площадок, 692 сегмента, 162 перехода и 32 зоны — всего 854 объекта дорожек и переходов. Все 1906 полных физических пар площадок сохранены; два отрицательных контроля теряют 11 и 13 обязательных пар. Текущее изменение и проверки сохранности описаны в [`docs/research/MAX31856_CS_PULLUP_LAYOUT_RU.md`](docs/research/MAX31856_CS_PULLUP_LAYOUT_RU.md), машинные результаты находятся в [`docs/research/tc_cs/`](docs/research/tc_cs/). Отчёты FUEL_CMD/GLOW_CMD, ROOM_DQ и HC160 сохранены как история предыдущих принятых шагов. Это проверка CAD, не электрический, тепловой, механический или производственный допуск.

В предшествующей дискретной ревизии двух каналов MAX31856 BIAS соединён с T−, а C9/C14 по 0,1 мкФ включены между T− и T+. Основание и проверка: [`docs/research/THERMOCOUPLE_BIAS_CORRECTION_RU.md`](docs/research/THERMOCOUPLE_BIAS_CORRECTION_RU.md). Схемная замена на J30/J31: [`docs/research/MAX31856_MODULE_SCHEMATIC_INTEGRATION_RU.md`](docs/research/MAX31856_MODULE_SCHEMATIC_INTEGRATION_RU.md); назначение колодок: [`docs/research/MAX31856_SOCKET_PCB_APPLIED_RU.md`](docs/research/MAX31856_SOCKET_PCB_APPLIED_RU.md). Существующий схемный PDF предшествует назначению Footprint/MPN и служит только обзором, а не точным актуальным экспортом или производственным файлом.

Актуальные материалы:

- [`hardware/kicad/ELECTRICAL_INTERFACE_RU.md`](hardware/kicad/ELECTRICAL_INTERFACE_RU.md) — действующая карта GPIO, клемм и релейной логики;
- [`docs/research/C104_BOOTSTRAP_REVIEW_RU.md`](docs/research/C104_BOOTSTRAP_REVIEW_RU.md) — принятая C0G-позиция C104 и ограничения её проверки;
- [`docs/research/THERMOCOUPLE_BIAS_CORRECTION_RU.md`](docs/research/THERMOCOUPLE_BIAS_CORRECTION_RU.md) — принятая коррекция BIAS и дифференциальных C9/C14;
- [`docs/research/THERMOCOUPLE_ACQUISITION_CONTRACT_RU.md`](docs/research/THERMOCOUPLE_ACQUISITION_CONTRACT_RU.md) — задание для будущей прошивки; не реализация и не допуск горения;
- [`docs/research/MAX31856_MODULE_SCHEMATIC_INTEGRATION_RU.md`](docs/research/MAX31856_MODULE_SCHEMATIC_INTEGRATION_RU.md) — точная схемная замена U9/U10 на J30/J31 и пределы её проверки;
- [`docs/research/MAX31856_MODULE_PCB_INTEGRATION_RU.md`](docs/research/MAX31856_MODULE_PCB_INTEGRATION_RU.md) — предшествующая модульная разводка J30/J31, DRC/ERC, связность и ограничения;
- [`docs/research/MAX31856_CS_PULLUP_LAYOUT_RU.md`](docs/research/MAX31856_CS_PULLUP_LAYOUT_RU.md) и [`docs/research/tc_cs/`](docs/research/tc_cs/) — текущий перенос R3 и сокращение TC_AIR_CS, точные хеши, DRC/ERC и проверка сохранности;
- [`docs/research/RELAY_COMMAND_ROUTES_APPLIED_RU.md`](docs/research/RELAY_COMMAND_ROUTES_APPLIED_RU.md) и [`docs/research/logic/`](docs/research/logic/) — историческая приёмка предыдущего шага FUEL_CMD/GLOW_CMD;
- [`docs/research/DS18B20_ROUTE_APPLIED_RU.md`](docs/research/DS18B20_ROUTE_APPLIED_RU.md) и [`docs/research/room/`](docs/research/room/) — историческая приёмка предыдущего шага ROOM_DQ;
- [`docs/research/HC160_LAYOUT_APPLIED_RU.md`](docs/research/HC160_LAYOUT_APPLIED_RU.md) и [`docs/research/relayout/`](docs/research/relayout/) — историческая приёмка шага с переносом J29;
- [`docs/research/MAX31856_SOCKET_PCB_APPLIED_RU.md`](docs/research/MAX31856_SOCKET_PCB_APPLIED_RU.md) — историческая приёмка установленных посадочных мест J30/J31 до текущей перекомпоновки;
- [`docs/research/RELAY_DG85BM_COIL_ENVELOPE_REVIEW_RU.md`](docs/research/RELAY_DG85BM_COIL_ENVELOPE_REVIEW_RU.md) и [`docs/research/RELAY_DG85BM_COIL_ENVELOPE_REVIEW.json`](docs/research/RELAY_DG85BM_COIL_ENVELOPE_REVIEW.json) — пересчёт двух реле, драйвера и горячего втягивания;
- [`docs/REQUIREMENTS_RU.md`](docs/REQUIREMENTS_RU.md) — требования и незакрытые условия;
- [`docs/BOM_PURCHASE_NOTES_RU.md`](docs/BOM_PURCHASE_NOTES_RU.md) — единственный актуальный указатель модульной BOM: 65 схемных позиций, 44 сгруппированные закупочные строки и 68 физических изделий PCB; внешние модули/датчики и механические интерфейсы вынесены отдельно, разрешения заказа нет;
- [`docs/board-front-module-wip.png`](docs/board-front-module-wip.png) и [`docs/board-back-module-wip.png`](docs/board-back-module-wip.png) — текущие виды PCB после переноса R3; B.Cu показан в координатах платы без зеркалирования;
- [`manufacturing/DRAFT_NOT_FOR_ORDER/`](manufacturing/DRAFT_NOT_FOR_ORDER/) — Gerber, Excellon, placement и сборочные PDF предшествующей дискретной PCB; устаревшие копии BOM удалены, остальные файлы не относятся к целевой модульной ревизии;
- [`manufacturing/DRAFT_NOT_FOR_ORDER/SHA256SUMS`](manufacturing/DRAFT_NOT_FOR_ORDER/SHA256SUMS) — контрольные суммы комплекта.

Согласованной прошивки нет. Электрическая и тепловая квалификация, HC160, силовые режимы и физические испытания не закрыты. Ревизионные копии рядом с исходниками не хранятся; история — в Git.
