# KiCad — единственный действующий проект

**DRAFT / REVIEW REQUIRED / NOT FOR ORDER / NO POWER / NO FLASH / NO FIRE; firmware HOLD.**

Открывайте `Sirokko26201_control_motor_candidate.kicad_pro` в KiCad 10.0.8. В этом каталоге находятся единственные действующие `.kicad_pro`, корневая схема и PCB, а также подключённые дочерние листы, проектные символы, посадочные места и локальные 3D-модели.

Контрольные суммы:

- `.kicad_pro`: `11029d86464484cea63d73aa802bad95c1a6805aee11e841fc78322e9d2bc0d6`;
- root SCH: `0f62b97606d5220ebe1cb630b59fd2050777e6d9020e623b93a4c5518774f76e`;
- PCB: `57f4b9437de4176f61e99d71ef923711f1368ba13d1de3b72c6fd13d82a4d09b`;
- `fp-lib-table`: `c35e63ee4ee827fe81aeac1b0c232e65df929c9dd8eb3dcb357258ef391c0ca2`.

PCB имеет контур 305 × 170 мм. Все 72 посадочных места находятся на F.Cu; J1=(100,45; 75,5) мм и J3=(100,45; 100,9) мм, обе колодки −90°. Состав: 72 посадочных места, 291 площадка, 639 сегментов, 146 переходов, 78 сетей и 27 зон. В `/TC_SDI_MOSI` удалён один переход без увеличения длины сети; остальные цепи сохранены после не прошедших DRC/связность проб. R108/C109 и C24 `CL21B224KCFSFNE` не менялись. F1 — Littelfuse `0997015.WXN`, 15 А / 58 В DC, в прежней посадке Keystone 3568. Native ERC/DRC из полного каталога проекта: код 0, 0 нарушений, 0 разрывов и 0 расхождений PCB↔SCH. Подробности: [VIA_REDUCTION_CURRENT_RU.md](../../docs/research/VIA_REDUCTION_CURRENT_RU.md) и [dependency-audit.json](../../manufacturing/current/checks/dependency-audit.json).

Проектные зависимости разрешаются через `${KIPRJMOD}` в `fp-lib-table` и `sym-lib-table`; стандартные модели — через `${KICAD10_3DMODEL_DIR}`. Неиспользуемая запись отсутствующей библиотеки `Sirokko_Display.pretty` удалена; все оставшиеся `${KIPRJMOD}`-пути существуют в проекте. Все используемые проектные символы, посадочные места и локальные модели включены; пять необязательных ссылок на стандартные 3D-модели отсутствуют в установленном KiCad 10.0.8 и перечислены в [`dependency-audit.json`](../../manufacturing/current/checks/dependency-audit.json). Это не мешает открытию, ERC/DRC и редактированию, но 3D-вид не является доказательством полной геометрии этих пяти корпусов. Имена отдельных дочерних листов и библиотек содержат `DRAFT`, потому что инженерная приёмка не завершена; это не параллельные проекты.

Действующая карта соединений: [ELECTRICAL_INTERFACE_RU.md](ELECTRICAL_INTERFACE_RU.md) и [внешние подключения](../../docs/research/CONTROL_CONNECTIONS_CURRENT_RU.md). Закупочный состав остаётся черновым: [BOM_PURCHASE_NOTES_RU.md](../../docs/BOM_PURCHASE_NOTES_RU.md). Проверочный CAM/BOM находится в [`manufacturing/current`](../../manufacturing/current/) и не разрешён к заказу. Прежние состояния доступны в истории Git.
