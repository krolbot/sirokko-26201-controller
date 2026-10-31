# Sirokko — два готовых MAX31856: замена в полной схеме

Статус: **DRAFT / NOT FOR ORDER / NO POWER / NO FLASH / NO FIRE; firmware HOLD.**

Схемная замена на два готовых модуля проверена в полном проекте. After6 принят для публикации как DRAFT/WIP после исправления только выравнивания заголовка MinimalSafety. Основная PCB развивается отдельно как модульный WIP; производственный комплект и прошивка не готовы.

## Источник и электрическое изменение

Исходная корневая SCH SHA256: `93e7b1063e20527aa473c37a7c1c3760ab218979f3829dc23b469994a237ae1c`.

Текущая корневая SCH after6 не изменилась относительно accepted after4:

- Lab: `/projects/exports/sirokko_max31856_modules/project/Sirokko26201_control_motor_candidate.kicad_sch`;
- 186 302 байта;
- SHA256 `cd57f2f4cb95d1837588f269e49d26bc122facc66959b843bc91a59548b22ec4`.

Дочерняя `MinimalSafety.kicad_sch` after6:

- Lab: `/projects/exports/sirokko_max31856_modules/project/MinimalSafety.kicad_sch`;
- 38 524 байта;
- SHA256 `3419aa24e854817fa59c91646ded28834a511ab17568cda566763b223c3f3ef0`;
- заголовок `SIROKKO: MINIMAL SAFETY CORE - U11 DRV8803 RELAY DRIVER ONLY` оставлен на `(30.48, 20.32)` и выровнен влево;
- старый заголовок `SIROKKO: MINIMAL SAFETY CORE - 3 IC + EXISTING RELAY DRIVER` отсутствует;
- устаревшие relay-permit/Q и TPS3431 fixed-WDT подписи отсутствуют.

Электрическая архитектура относительно accepted after4 не изменилась. Ранее удалены U9/U10, J10/J11 и C9–C18 — 14 заменённых компонентов, а также относящиеся к ним 44 локальных провода, 48 меток, 4 NC и 8 junction. В корневой схеме остаются J30 — MAX31856 EXHAUST и J31 — MAX31856 AIR с отдельными CS/DRDY и общей SPI.

Распиновка обоих разъёмов:

- 1: DRDY, отдельный канал;
- 2: FLT, NC;
- 3: CS, отдельный канал;
- 4: SDI/MOSI;
- 5: SDO/MISO;
- 6: SCK;
- 7: GND;
- 8: 3Vo, NC;
- 9: VIN, +3V3.

Термопары подключаются непосредственно к клеммам модулей. R2/R3 остаются подтяжками CS; J12/DS18B20 и R1 не изменены. Геометрия модулей Adafruit 3263 ранее принята пользователем.

## Принятые after6-файлы

Удалённые файлы и локальные копии совпали по SHA256 и размеру:

- PDF: `/projects/exports/sirokko_max31856_modules/bfe3c062_after6/pdf/Sirokko26201_control_motor_candidate.pdf`, 289 781 байт, SHA256 `fdffa077e360784c34e0d2bfa52cea9376f02cd838abffd45534d74d13ff55e7`;
- MinimalSafety SVG: `/projects/exports/sirokko_max31856_modules/bfe3c062_after6/svg/Sirokko26201_control_motor_candidate-MinimalSafety.svg`, 820 062 байта, SHA256 `d346782b091a1559e93ea4b49489080fc7222e6c024fa53e41af7ad9d5efac9d`;
- XML: `/projects/exports/sirokko_max31856_modules/bfe3c062_after6/netlist/Sirokko26201_control_motor_candidate.xml`, 127 882 байта, SHA256 `79c70397e057e0786a592243e37ba1f0a9378b3eeabad2cf984a7d256764f758`;
- ERC: `/projects/exports/sirokko_max31856_modules/bfe3c062_after6/erc/Sirokko26201_control_motor_candidate-erc.json`, 1 854 байта, SHA256 `d7c93113873d10d0c678939567580ad864eacb096e2f93dbb09ac26383b9f5b1`.

## Электрическая проверка

After6 XML отличается от accepted after4 XML только `design/date`: `2026-10-31T16:35:18` → `2026-10-31T16:59:42`. После буквальной нормализации только этого узла файлы побайтово равны; нормализованный SHA256 обоих — `04d2901cb85ff75ea12cf558b58f567112df52b903163f5373b9b4ac3615ad9c`.

Состав неизменен: 5 листов, 65 компонентов, 80 сетей, 257 назначений pin→net. KiCad 10.0.7 ERC: 5 листов, 0 нарушений. Новых исключений ERC не добавлялось. Это не подтверждение работоспособности или безопасности изделия.

## Визуальная проверка листа 4

PDF содержит ровно 5 листов. Лист 4 отрендерен при 180 dpi: 2977×2105, SHA256 PNG `7857e1b8294983e9ebf2e9831ec396d5de8bed51fc9e5500367955af007277bc`.

Проверка:

- полный новый заголовок виден внутри внутренней рамки — PASS;
- обе актуальные строки `CURRENT CONTRACT...` и `FUTURE FIRMWARE REQUIREMENT...` полностью видны внутри рамки — PASS;
- прежний заголовок с `3 IC` отсутствует — PASS;
- устаревшие `Q -> safety relay IN3; /Q -> DRV8803 nENBL(8)` и `TPS3431 fixed WDT: 170..230ms...` отсутствуют — PASS.

After6-файлы разрешены к переносу publisher t_574641f7 как DRAFT/WIP.

## PCB и пределы приёмки

Текущая Lab PCB — ранее принятый модульный WIP, не изменённый этой текстовой правкой:

- `/projects/exports/sirokko_max31856_modules/project/Sirokko26201_control_motor_candidate.kicad_pcb`;
- SHA256 `efdae16f81208a62a88358b7bf35f454e072378b686593ecc42d9b9f1dcfb700`.

Этот отчёт не переоценивает размещение, трассировку, DRC/parity, крепления, высоты, BOM/CAM, тепловую квалификацию или прошивку модульной PCB. F8, CAM/BOM, прошивка, питание и огневые испытания не выполнялись.

Ограничения сохраняются: **DRAFT / NOT FOR ORDER / NO POWER / NO FLASH / NO FIRE; firmware HOLD.**
