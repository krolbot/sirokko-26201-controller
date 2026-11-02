# KiCad — единственный действующий проект

**DRAFT / REVIEW REQUIRED / NOT FOR ORDER / NO POWER / NO FLASH / NO FIRE; firmware HOLD.**

Открывайте `Sirokko26201_control_motor_candidate.kicad_pro` в KiCad 10.0.8. В этом каталоге находятся единственные действующие `.kicad_pro`, корневая схема и PCB, а также подключённые дочерние листы, проектные символы, посадочные места и локальные 3D-модели.

Контрольные суммы:

- `.kicad_pro`: `11029d86464484cea63d73aa802bad95c1a6805aee11e841fc78322e9d2bc0d6`;
- root SCH: `c8c5bd6e2bc0cb61428037c93b54b6c5d26ea97d9e6529f3facb100935ee3b3b`;
- PCB: `46b84ef09d00a724962753ac0e83492b63a43177d69b33a519c890d5be9a0820`;
- `fp-lib-table`: `8e9cf2caf9090ad7d5e78625f981dc0fe8f22895f48515eb55261a27f1c247ac`.

PCB имеет контур 305 × 170 мм. Все 72 посадочных места находятся на F.Cu; J1=(100,45; 75,5) мм и J3=(100,45; 100,9) мм, обе колодки −90°. Состав: 72 посадочных места, 291 площадка, 639 сегментов, 147 переходов, 78 сетей и 27 зон. У 75 безопасных переходов 0,60/0,30 мм площадка увеличена до 0,80/0,30 мм; 28 конфликтующих переходов оставлены без изменения. R108/C109 интегрированы рядом с U13; C24 заменён на `CL21B224KCFSFNE` без изменения посадки и сетей. Native ERC/DRC из полного каталога проекта: код 0, 0 нарушений, 0 разрывов и 0 расхождений PCB↔SCH. Staged Pre-fab Gate запускал parity без исключений, но его `passed=true` не подменяет отдельную проверку библиотек из-за предупреждений изолированной конфигурации. Подробности: [PCB_SCH_PARITY_CURRENT_RU.md](../../docs/research/PCB_SCH_PARITY_CURRENT_RU.md) и [dependency-audit.json](../../manufacturing/current/checks/dependency-audit.json).

Проектные зависимости разрешаются через `${KIPRJMOD}` в `fp-lib-table` и `sym-lib-table`; стандартные модели — через `${KICAD10_3DMODEL_DIR}`. Все проектные символы, посадочные места и локальные модели включены; пять необязательных ссылок на стандартные 3D-модели отсутствуют в установленном KiCad 10.0.8 и перечислены в [`dependency-audit.json`](../../manufacturing/current/checks/dependency-audit.json). Это не мешает открытию, ERC/DRC и редактированию, но 3D-вид не является доказательством полной геометрии этих пяти корпусов. Имена отдельных дочерних листов и библиотек содержат `DRAFT`, потому что инженерная приёмка не завершена; это не параллельные проекты.

Действующая карта соединений: [ELECTRICAL_INTERFACE_RU.md](ELECTRICAL_INTERFACE_RU.md) и [внешние подключения](../../docs/research/CONTROL_CONNECTIONS_CURRENT_RU.md). Закупочный состав остаётся черновым: [BOM_PURCHASE_NOTES_RU.md](../../docs/BOM_PURCHASE_NOTES_RU.md). Проверочный CAM/BOM находится в [`manufacturing/current`](../../manufacturing/current/) и не разрешён к заказу. Прежние состояния доступны в истории Git.
