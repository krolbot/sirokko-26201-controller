# Sirokko b64cbaa9 — DRAFT / NOT FOR ORDER / NO POWER / NO FIRE

PCB SHA256 b64cbaa99161242f4f52cd8409a385a1a736106f1efe0df467dbbb6747b32d3a.
Основа 3af3e417. Локальная разводка детектора основной шины:
- P24_ZENER_K: R121.2/D20.1.
- P24_BASE: D20.2/Q20.1/R123.1.
- P24_SENSE_N: Q20.3/R122.2/U20.6.
- GND: Q20.2/R123.2 соединены с прежним основным возвратом, проверенным через C106.2.
Добавлено 17 сегментов 0,25 мм и 1 via 0,60/0,30 мм; прежние объекты не удалялись и не двигались. Выводы U20.1/.5 — питание/логический HIGH по существующей схеме; их питание, C28 и P24_PRESENT к U6 ещё не выполнены. Входная последовательная цепь до R121.1 ещё не закончена. Падение основной шины и запрет повторного взведения не квалифицированы — узел не считается законченным детектором.
Свежая заливка и полный MCP DRC: 163 unconnected / 26 прежних warnings, полный multiset violations совпал с baseline. Правила не ослаблялись. Native source/candidate и повторное открытие master: 699 pads, 384→377 groups, 7086→7296 physical pairs, 0 lost, 210 expected additions. Все additions равны только объединению старых групп четырёх перечисленных цепей. Отрицательный контроль исходных singleton площадок прошёл. Большое число пар — следствие присоединения двух площадок к большой группе GND, не 210 новых маршрутов.
Сохранены footprints/pads/UUID/nets/positions/прежняя медь/геометрия zones/keepouts/Edge.Cuts, прежние safety/control/TFT группы, 9 thermal-via и две EP/GND zones. Master reopen: 1613 tracks+vias, 27 zones. SCH/PRO и firmware не менялись. Native exit 0 с прежней PROPERTY_ENUM diagnostic. CLIexit/ERC/parity не подтверждены; это не production PASS. Backup 3af3e417 сохранён.
Архив — delta PCB и evidence, не полный проект и не производственный комплект. Нового изображения нет. Старое не выдавать за изображение текущей ревизии. Физическое включение/прошивка/огневые испытания не выполнялись; firmware integration после принятой полной PCB.
