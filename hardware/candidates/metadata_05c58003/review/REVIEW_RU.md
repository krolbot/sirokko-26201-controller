# Sirokko — независимая приёмка NEXT5 / metadata19

DRAFT / NOT FOR ORDER / НЕ ДЛЯ ОГНЕВЫХ ИСПЫТАНИЙ.

## Принятый ограниченный результат

Проверен замороженный пакет `SIROKKO_NEXT5_METADATA19_T_17025A35_DRAFT_NOT_FOR_ORDER.zip` (сам архив в репозиторий не включён).
SHA256 68147c92c77a079d20045970b33e9945543a5c80e2bee248d57f35de9d972c82. Собственные проверки координатора: ZIP CRC PASS; 128 файлов; все 127 записей PACKAGE_MANIFEST.json совпали по размеру и SHA. Все 76 файлов проекта совпали с after-SHA из source manifest; все пять дочерних Sheetfile корня реально вложены.

Актуальная парная ревизия:
- PCB 05c58003722be967ed31e54bef893a04c161347910efcb29c9e27a26fb0b0ab2;
- root SCH 3fa9d05afd6750a27b56ec9dd9dad9b9e80d0631f0fe072cb2c67f2f5674aa50;
- child TFT reset SCH 5440a7318222fc539a87b6a25773cc005a77c1ec5f0411951747006176d8b939.

## Независимое сравнение источников и связности

1. Из CSV непосредственно пересчитано: BOM 128 строк, 110 заполненных MPN и 18 пустых. Delta к before CSV ровно R1/R16/R17/R85/R303, только MPN/Manufacturer/Datasheet. Другие строки, включая прежние 105 MPN, идентичны.
2. Из двух XML заново построены полные словари (reference,pin)→net: 467 записей, равны.
3. Новый PCB сравнен с реально прочитанным принятым Reference21 ZIP: исходный SHA 4f626502ad177c08a06b949cee6a78ef300543a7075aa1c78efacd1c989b14eb. После исключения только блоков свойств MPN/Manufacturer/Datasheet все 146042 остальных лексических токена тождественны. Следовательно геометрия, площадки/сети/UUID, медь, графика, Reference/Value, PH2, keepouts и контур этим этапом не изменены.
4. По фактическим footprint-блокам changed refs ровно R1…R17/R85/R303 (19). Для импортированных MLCC Reference находится в fp_text reference, а не property Reference: первый аналитический проход ошибся на этой разнице; после учёта обеих штатных форм проверка выполнена.
5. В двух native connectivity evidence полностью равны pads, footprints, components, pairs и cross_net_pairs. Координатор отдельно восстановил все попарные сочетания из native partition: 533 pads, 292 компоненты связности, 5646 пар; восстановленное множество совпало с supplied native pairs. Cross-net pairs=0. Это чтение и независимая перепроверка доставленного native результата, НЕ новый запуск pcbnew на активном сервере.

## DRC/ERC

Прочитаны реальные raw JSON из нового архива:
- DRC: 27 обычных warnings, 120 unconnected errors, 14 parity warnings, 5 ignored-check types; exit 5.
- Parity: 11 extra_footprint + 3 footprint_symbol_mismatch. Field mismatch=0.
- ERC: 6 sheets, 8 warnings, 0 errors; 4 ignored-check types; exit 5.

Raw violations DRC полностью идентичны Reference21 baseline. Parity 28→14 удалил ровно 14 footprint_symbol_field_mismatch. 120 unconnected сохранены. Это подтверждает ограниченную metadata-синхронизацию, НЕ завершение разводки.

## Изображения

Из архива извлечены фактические top/bottom PNG 3168×2384. Оба осмотрены: четыре кромки без обрезки; компоненты и монтажные отверстия видимы. Мелкая маркировка, особенно правый нижний узел, остаётся плотной/малоконтрастной. Картинки не являются сборочным чертежом и не подтверждают комплектность реальных реле, установленных модулей или токовую квалификацию.

Для выдачи поверх неизменённого родного изображения добавлена отдельная полоса высотой 100 px с DRAFT / NOT FOR ORDER / NOT FOR FIRE TESTING и SHA-префиксом. Файлы Sirokko_top_05c58003_DRAFT_LABELED.png и Sirokko_bottom_05c58003_DRAFT_LABELED.png; native PCB не менялась.

## Продолжение критического пути

Этот metadata handoff ограниченно принят. Продолжать полную компоновку в t_fbad33a1 по новому SHA, а не создавать очередной metadata writer или откатывать SCH371c/PCB4f626502. Перед записью сверить live SHA и отсутствие другого писателя. Следующие незакрытые функциональные узлы: реальная relay/socket пара, вход защиты/ввода и fuse, окончательная кабельная и высотная сборка TFT/панели. Не прятать остальные parity, ERC, семь прежних text-height и другие DRC. Routing, firmware и production release остаются HOLD. Push новой ревизии здесь не проверен и не заявлен.
