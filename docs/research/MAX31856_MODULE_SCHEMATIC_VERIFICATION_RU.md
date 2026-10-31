# Sirokko MinimalSafety after6 — инженерная приёмка

Результат: **PASS. Комплект after6 разрешён к передаче publisher t_574641f7 как DRAFT/WIP.**

Ограничения сохраняются: **DRAFT / NOT FOR ORDER / NO POWER / NO FLASH / NO FIRE; firmware HOLD.** Это не разрешение на заказ, питание, прошивку или огневые испытания.

## Исправление

В Lab-источнике `/projects/exports/sirokko_max31856_modules/project/MinimalSafety.kicad_sch` изменено только представление заголовка UUID `42f3f734-17ec-4cce-a55a-9b50806ac100`: к прежней точке `(30.48, 20.32)` добавлено выравнивание `left`. Текст сохранён без изменения:

`SIROKKO: MINIMAL SAFETY CORE - U11 DRV8803 RELAY DRIVER ONLY`

Электрические объекты, root SCH, PCB, firmware, F8 и CAM/BOM не менялись.

## Файлы after6

Удалённые файлы и выгруженные локальные копии совпали по SHA256 и размеру:

- MinimalSafety SCH: `/projects/exports/sirokko_max31856_modules/project/MinimalSafety.kicad_sch`, 38 524 байта, SHA256 `3419aa24e854817fa59c91646ded28834a511ab17568cda566763b223c3f3ef0`;
- root SCH: `/projects/exports/sirokko_max31856_modules/project/Sirokko26201_control_motor_candidate.kicad_sch`, 186 302 байта, SHA256 `cd57f2f4cb95d1837588f269e49d26bc122facc66959b843bc91a59548b22ec4`;
- PDF: `/projects/exports/sirokko_max31856_modules/bfe3c062_after6/pdf/Sirokko26201_control_motor_candidate.pdf`, 289 781 байт, SHA256 `fdffa077e360784c34e0d2bfa52cea9376f02cd838abffd45534d74d13ff55e7`;
- MinimalSafety SVG: `/projects/exports/sirokko_max31856_modules/bfe3c062_after6/svg/Sirokko26201_control_motor_candidate-MinimalSafety.svg`, 820 062 байта, SHA256 `d346782b091a1559e93ea4b49489080fc7222e6c024fa53e41af7ad9d5efac9d`;
- XML: `/projects/exports/sirokko_max31856_modules/bfe3c062_after6/netlist/Sirokko26201_control_motor_candidate.xml`, 127 882 байта, SHA256 `79c70397e057e0786a592243e37ba1f0a9378b3eeabad2cf984a7d256764f758`;
- ERC JSON: `/projects/exports/sirokko_max31856_modules/bfe3c062_after6/erc/Sirokko26201_control_motor_candidate-erc.json`, 1 854 байта, SHA256 `d7c93113873d10d0c678939567580ad864eacb096e2f93dbb09ac26383b9f5b1`.

PDF содержит 5 листов.

## Электрическая неизменность

Root SCH побайтово совпал с accepted after4: SHA256 `cd57f2f4cb95d1837588f269e49d26bc122facc66959b843bc91a59548b22ec4`.

After6 XML отличается от accepted after4 XML только `design/date`: `2026-10-31T16:35:18` → `2026-10-31T16:59:42`. После буквальной замены содержимого только этого узла на одинаковое значение файлы побайтово равны; нормализованный SHA256 обоих — `04d2901cb85ff75ea12cf558b58f567112df52b903163f5373b9b4ac3615ad9c`.

Состав неизменен: 5 листов, 65 компонентов, 80 сетей, 257 назначений pin→net.

Native KiCad 10.0.7 ERC: 5 листов, 0 нарушений — **PASS**.

Текущая PCB не менялась: `/projects/exports/sirokko_max31856_modules/project/Sirokko26201_control_motor_candidate.kicad_pcb`, SHA256 `efdae16f81208a62a88358b7bf35f454e072378b686593ecc42d9b9f1dcfb700`.

## Визуальный gate листа 4

Лист 4 отрендерен из свежего PDF при 180 dpi: 2977×2105 px, PNG SHA256 `7857e1b8294983e9ebf2e9831ec396d5de8bed51fc9e5500367955af007277bc`.

Независимая визуальная проверка:

- полный заголовок виден и находится внутри внутренней рамки — PASS;
- оценочные границы рамки: x=84…2892 px; заголовка: x=216…695 px;
- `CURRENT CONTRACT...` полностью видна внутри рамки — PASS;
- `FUTURE FIRMWARE REQUIREMENT...` полностью видна внутри рамки — PASS;
- прежний заголовок с `3 IC` отсутствует — PASS;
- устаревшая строка relay-permit/Q отсутствует — PASS;
- устаревшая строка `TPS3431 fixed WDT...` отсутствует — PASS. Упоминание TPS3431 остаётся только в актуальной строке `REMOVED / CANCELLED`.

## Передача publisher

Разрешено перенести из этой задачи точные after6-копии MinimalSafety SCH, пятилистового PDF, MinimalSafety SVG, XML, ERC JSON, PNG визуальной проверки, `verification_after6.json`, этот отчёт и обновлённый `MAX31856_MODULE_SCHEMATIC_INTEGRATION_RU.md`.

Publisher должен обновить прежние accepted after4/отклонённые after5 SHA и утверждения только на приведённые здесь after6 SHA. Принятый PCB WIP SHA `efdae16f…` сохранить.
