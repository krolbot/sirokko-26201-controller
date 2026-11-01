# KiCad — модульная схема и PCB

**DRAFT / NOT FOR ORDER / NO POWER / NO FLASH / NO FIRE.**

J30/J31 используют `SSW-109-01-T-S` и посадочное место `Connector_Sirokko_Module:Samtec_SSW-109-01-T-S_1x09_P2.54mm_D1.04mm`; все 18 сверлений имеют Ø1,04 мм. После переноса J29 и переразводки `/ROOM_DQ`, `/FUEL_CMD` и `/GLOW_CMD` текущий шаг перенёс R3 к ESP32 и сократил `/TC_AIR_CS` с 5 до 1 перехода; всего число переходов сократилось с 210 до 162. Текущие хеши, проверки сохранности и ограничения: [MAX31856_CS_PULLUP_LAYOUT_RU.md](../../docs/research/MAX31856_CS_PULLUP_LAYOUT_RU.md). Приёмки прежних шагов сохранены в [RELAY_COMMAND_ROUTES_APPLIED_RU.md](../../docs/research/RELAY_COMMAND_ROUTES_APPLIED_RU.md), [DS18B20_ROUTE_APPLIED_RU.md](../../docs/research/DS18B20_ROUTE_APPLIED_RU.md), [HC160_LAYOUT_APPLIED_RU.md](../../docs/research/HC160_LAYOUT_APPLIED_RU.md) и [MAX31856_SOCKET_PCB_APPLIED_RU.md](../../docs/research/MAX31856_SOCKET_PCB_APPLIED_RU.md).

Кандидат без `P24_PRESENT` опубликован отдельно: [архив исходников и CAM](../../manufacturing/REVIEW_REQUIRED_NO_P24_2026-11-01/) и [отчёт проверки](../../docs/research/no_p24/NO_P24_CANDIDATE_2026-11-01_RU.md). В нём `U20/Q20/D20/R120–R123/C28` и их медь удалены, `J1.6/GPIO6` — NC. Метка `P24_PRESENT` после этой операции только историческая/зарезервированная и не входит в текущую спецификацию ввода-вывода. Кандидат не заменяет файлы этого каталога до человеческой приёмки и имеет статус `REVIEW REQUIRED / NOT FOR ORDER`.

Откройте `Sirokko26201_control_motor_candidate.kicad_pro` в KiCad 10.0.8. Корневая схема, PCB, все используемые дочерние листы, таблицы библиотек, проектные символы, посадочные места и локальные 3D-модели находятся в этом каталоге. Это единственный опубликованный CAD-путь. Схемный WIP содержит новую ветвь R108/C109 22 кОм/33 нФ, а PCB осталась прежней без этой пары. Проверенный CAM относится к прежней разводке и опубликован отдельным [черновым архивом](../../manufacturing/DRAFT_NOT_FOR_ORDER/SIROKKO_CAM_CURRENT_DRAFT.zip); согласованного SCH/PCB-комплекта для RC-ветви и производственного выпуска нет.

Точки входа:

- `Sirokko26201_control_motor_candidate.kicad_pro`;
- `Sirokko26201_control_motor_candidate.kicad_sch`;
- `Sirokko26201_control_motor_candidate.kicad_pcb`.

Имена отдельных подключённых листов содержат `DRAFT`, потому что электрическая и производственная приёмка не завершена. Эти файлы входят в действующую иерархию и не являются параллельными историческими проектами.

Проектные зависимости разрешаются через `${KIPRJMOD}` в `fp-lib-table` и `sym-lib-table`; стандартные модели — через `${KICAD10_3DMODEL_DIR}`.

Экран и его крепёж исключены новым требованием пользователя. Ранее выбранные Waveshare, Adafruit, DFR0650 и OLED 9001709304 больше не являются действующим выбором. Их посадочные места, отдельная медь и экранные крепления удалены из основной PCB.

Прошивка отсутствует намеренно. Её новая реализация начинается после окончательной приёмки PCB и карты выводов. Проверенная таблица контактов, релейная логика и пределы питания: [ELECTRICAL_INTERFACE_RU.md](ELECTRICAL_INTERFACE_RU.md). Для J29 артикул Phoenix Contact 1715747 внесён в схему и PCB. Текущий C104 — Murata `GRM1885C1H222JA01D`, 2,2 нФ, 50 В, C0G, ±5%; основание и ограничения вынесены в [C104_BOOTSTRAP_REVIEW_RU.md](../../docs/research/C104_BOOTSTRAP_REVIEW_RU.md). Опубликованная модульная [BOM](../../docs/BOM_PURCHASE_NOTES_RU.md) относится к прежней PCB: 65 схемных позиций, 44 сгруппированные закупочные строки и 68 физических изделий PCB; R108/C109 в неё не входят. Это DRAFT / NOT FOR ORDER, не окончательная закупочная ведомость.

В предшествующем дискретном снимке были исправлены оба входа MAX31856: U9.2/U10.2 BIAS соединены с соответствующим T−, C9/C14 включены между T− и T+. Основание: [THERMOCOUPLE_BIAS_CORRECTION_RU.md](../../docs/research/THERMOCOUPLE_BIAS_CORRECTION_RU.md). Текущие схема и PCB заменили U9/U10, J10/J11 и C9–C18 двумя J30/J31 по принятой распиновке Adafruit 3263: общая SPI, отдельные CS/DRDY, FLT и `3Vo` NC, VIN=+3V3. Проверка схемы: [MAX31856_MODULE_SCHEMATIC_INTEGRATION_RU.md](../../docs/research/MAX31856_MODULE_SCHEMATIC_INTEGRATION_RU.md); предшествующая модульная разводка: [MAX31856_MODULE_PCB_INTEGRATION_RU.md](../../docs/research/MAX31856_MODULE_PCB_INTEGRATION_RU.md); текущие проверки: [DS18B20_ROUTE_APPLIED_RU.md](../../docs/research/DS18B20_ROUTE_APPLIED_RU.md). Модульная BOM и CAM текущей PCB опубликованы как проверенные черновики; это не закупочный или производственный допуск. Прошивочный контракт опроса сохранён отдельно: [THERMOCOUPLE_ACQUISITION_CONTRACT_RU.md](../../docs/research/THERMOCOUPLE_ACQUISITION_CONTRACT_RU.md); это не реализованная прошивка.

Ревизионные копии, отчёты, рендеры и архивы рядом с проектом не хранятся. Для восстановления прежних состояний используется история Git.

## Актуальное управление: только удалённо через ESP32

Экран, кнопка Start/Stop и энкодер исключены. Их схема и отдельная обвязка удалены, освобождённые выводы ESP отмечены NC. Подробности: [REMOTE_ONLY_REQUIREMENTS_RU.md](REMOTE_ONLY_REQUIREMENTS_RU.md). Схема намеренно новее PCB на R108/C109; прежняя проверка согласованной пары находится в [MAX31856_CS_PULLUP_LAYOUT_RU.md](../../docs/research/MAX31856_CS_PULLUP_LAYOUT_RU.md), а [RELAY_COMMAND_ROUTES_APPLIED_RU.md](../../docs/research/RELAY_COMMAND_ROUTES_APPLIED_RU.md), [DS18B20_ROUTE_APPLIED_RU.md](../../docs/research/DS18B20_ROUTE_APPLIED_RU.md), [HC160_LAYOUT_APPLIED_RU.md](../../docs/research/HC160_LAYOUT_APPLIED_RU.md) и [PCB_REVIEW_RU.md](PCB_REVIEW_RU.md) сохранены как история предыдущих принятых состояний. Прошивка точки доступа ESP32 и браузерной панели не реализована; firmware HOLD сохраняется.

## Внешний моторный модуль

Встроенный силовой ШИМ удалён из схемы и платы. HC-160A-S2 подключается только управляющим жгутом к J29: `1=A1/FAN_REQUEST`, `2=A2/GND`, `3=PA/SPEED_CMD`, `4=G/GND`. Модуль не крепится к PCB; питание двигателя проходит вне неё. Подробности и ограничения: [HC160_INTERFACE_RU.md](HC160_INTERFACE_RU.md).

В текущей ревизии остаются K1 (свеча) и K2 (муфта); K3 и R703 удалены, K2.87 соединён напрямую с J28.1. GPIO21/MCU_PERMIT теперь NC, GPIO18 остаётся NC. Новый схемный кандидат имеет ERC=0; его полный netlist содержит 67 компонентов и 81 сеть. PCB остаётся двухслойной и побайтно неизменной, SHA-256 `6042da2346f1ef873cda83086b57a3c0f69b981a6719fdd80eba219d4ae1b81a`; прежние DRC=0 и parity=0 не проверяют добавленные только в SCH R108/C109. Это CAD-проверка; общая перекомпоновка, электрическая, тепловая и механическая приёмка, прошивка и производственный выпуск не завершены.

## Полный набор правил DRC

Полный DRC PCB `6042da23…b81a` вместе с прежним SCH `902d0681…c979` дал 0 нарушений, 0 обрывов, 0 расхождений и `ignored_checks=[]`. Текущий root SCH `3597b3c7…f7aa` и лист входной защиты `9a805427…7ded` содержат R108/C109; свежий root ERC дал 0 нарушений, но новый DRC/parity не заявляется, потому что PCB ещё не содержит эту пару. Это не производственный допуск.
