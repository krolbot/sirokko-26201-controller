# Sirokko 262.01 — контроллер автоматики

> **DRAFT / NOT FOR ORDER / NO POWER / NO FLASH / NO FIRE.** Плата не закончена, не имеет производственного допуска и не прошла физическую приёмку. Её нельзя заказывать, включать, прошивать или подключать к горелке.

В рабочем дереве оставлен один актуальный проект KiCad:

- проект: [`hardware/kicad/Sirokko26201_control_motor_candidate.kicad_pro`](hardware/kicad/Sirokko26201_control_motor_candidate.kicad_pro);
- схема: [`hardware/kicad/Sirokko26201_control_motor_candidate.kicad_sch`](hardware/kicad/Sirokko26201_control_motor_candidate.kicad_sch);
- плата: [`hardware/kicad/Sirokko26201_control_motor_candidate.kicad_pcb`](hardware/kicad/Sirokko26201_control_motor_candidate.kicad_pcb);
- интерфейс HC-160A-S2: [`hardware/kicad/HC160_INTERFACE_RU.md`](hardware/kicad/HC160_INTERFACE_RU.md);
- SHA-256 текущей схемы: `da4c369967fe13e25d25cd0f8f2cf3b1fae14dc50ef2acddf8c625d268300ecc`;
- SHA-256 неизменённой PCB: `b11c63de29efd64ce23d33bcd5e74f7ddecd919ec385467222ebf0c1102e6cf3`.

В схему внесён DFRobot DFR0650: SSD1306, OLED 128×64, I²C с обычным адресом `0x3C`, питание 3,3 В. DS1 пока задан как шестиконтактное гнездо: `1=+3V3`, `2=GND`, `3=OLED_SCL`, `4=OLED_SDA`, `5/6=NC`; SDA подключён к GPIO8/J1.12, SCL — к GPIO47/J3.17. C307 — 100 нФ, MPN `GRM188R71E104KA01D`, посадочное место `C_0603_1608Metric`.

**Обновлена только схема. PCB намеренно не менялась: на ней остаются прежние экран, компоненты, медь и служебные ссылки на старые дочерние листы. PCB не соответствует текущей схеме.** Прежний результат DRC относится к старой паре SCH/PCB и не подтверждает эту несовместимую WIP-пару. Для текущей схемы получен свежий ERC KiCad 10.0.7 без нарушений, но это не приёмка PCB и не производственный выпуск.

Гнездо и крепление DFR0650, механика, разводка I²C и токовый бюджет ещё не приняты. Официальные материалы оставлены только ссылками: [страница изделия](https://wiki.dfrobot.com/Monochrome_0.96_128x64_IIC_SPI_OLED_Display_SKU_DFR0650), [схема модуля](https://dfimg.dfrobot.com/wiki/21114/DFR0650_fermion-oled-display-module_schematics_V1.0.pdf), [размерный чертёж](https://dfimg.dfrobot.com/wiki/21114/DFR0650_fermion-oled-display-module_dimension_V1.0.pdf).

Согласованной с этим аппаратным контрактом прошивки нет. Работы над прошивкой заблокированы до обновления и приёмки PCB относительно схемы и окончательной карты выводов.

Актуальные документы:

- [`docs/REQUIREMENTS_RU.md`](docs/REQUIREMENTS_RU.md) — требования;
- [`docs/UX_PANEL_REQUIREMENTS_RU.md`](docs/UX_PANEL_REQUIREMENTS_RU.md) — панель и управление;
- [`docs/LAYOUT_FREEDOM_RU.md`](docs/LAYOUT_FREEDOM_RU.md) — допустимая свобода компоновки;
- [`docs/CURRENT_STATE_RU.md`](docs/CURRENT_STATE_RU.md) — текущее состояние и ограничения;
- [`hardware/kicad/README.md`](hardware/kicad/README.md) — открытие проекта и локальные зависимости.

Ревизионные копии, отчёты проверок, рендеры, снимки и архивы не хранятся рядом с исходниками. Единственный путь восстановления прежних состояний — обычная история Git.
