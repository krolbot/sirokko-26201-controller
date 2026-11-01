# Вывод исторических данных узла контроля 24 В из применения

Дата: 2026-11-01

Действующий CAD больше не содержит отдельного входа контроля наличия 24 В. `J1.6/GPIO6` — NC. Будущая прошивка не должна читать этот вход, блокировать START по нему или восстанавливать прежнюю формулу разрешения.

Следующие материалы сохранены только как замороженные доказательства прежних ревизий и не задают текущую схему, PCB, BOM, карту GPIO или требования прошивки:

- `docs/reviews/minimal_safety_3ic_draft/`;
- `docs/research/RELAY_COMMAND_ROUTES_APPLIED_RU.md`;
- `docs/research/logic/`, `room/`, `relayout/`, `tc_cs/`, `socket/`;
- `docs/research/MAX31856_MODULE_SCHEMATIC_NETLIST.xml`;
- прежние отчёты входной защиты, пусковой модели и бюджета 3,3 В в той части, где они перечисляют удалённые `U20/Q20/D20/R120–R123/C28` или прежнюю сеть GPIO6.

Числа этих документов не переписываются задним числом: они относятся к указанным в них SHA прежних плат и схем. Для текущего состояния применять только:

- `hardware/kicad/Sirokko26201_control_motor_candidate.kicad_sch`;
- `hardware/kicad/Sirokko26201_control_motor_candidate.kicad_pcb`;
- `hardware/kicad/ELECTRICAL_INTERFACE_RU.md`;
- `docs/CURRENT_STATE_RU.md`;
- `docs/BOM_PURCHASE_NOTES_RU.md`.

Производственного архива в текущем дереве нет; прежний комплект доступен только через историю Git.

После загрузки ESP32 действующее требование — продувка при выключенных свече и муфте и ожидание нового START. Firmware implementation HOLD, NO POWER, NO FLASH и NO FIRE сохраняются.
