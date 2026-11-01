# Sirokko 262.01 controller

> **DRAFT / NOT FOR ORDER / NO POWER / NO FLASH / NO FIRE; firmware HOLD.** The board is unfinished, has no manufacturing release, and has not passed physical acceptance. Do not order it, power it, flash it, or connect it to a burner. The schematic and PCB now contain the selected removable MAX31856 modules and the assigned Samtec sockets, and pass the current CAD parity checks. The tracked manufacturing package is still the preceding discrete-MAX31856 draft and does not match the current PCB.

The working tree contains one intentionally incomplete KiCad WIP:

- project: [`hardware/kicad/Sirokko26201_control_motor_candidate.kicad_pro`](hardware/kicad/Sirokko26201_control_motor_candidate.kicad_pro);
- schematic: [`hardware/kicad/Sirokko26201_control_motor_candidate.kicad_sch`](hardware/kicad/Sirokko26201_control_motor_candidate.kicad_sch);
- PCB: [`hardware/kicad/Sirokko26201_control_motor_candidate.kicad_pcb`](hardware/kicad/Sirokko26201_control_motor_candidate.kicad_pcb);
- current schematic SHA-256: `902d0681f072a57559e78aff44cfd5b877478634b8bae62d3e4e834592a3c979`;
- current PCB SHA-256: `6042da2346f1ef873cda83086b57a3c0f69b981a6719fdd80eba219d4ae1b81a`;
- current `fp-lib-table` SHA-256: `8e9cf2caf9090ad7d5e78625f981dc0fe8f22895f48515eb55261a27f1c247ac`;
- current power-sheet SHA-256: `36248b3d5b1e4c93b9551ad1ab19b3abd59f357226a311998b310b7e8ac1514d`.

The latest product decision selects two removable blue MAX31856 modules from <https://ozon.ru/t/3QtIbaX> instead of the on-board U9/U10 circuits. The exact module is not MAX31865. The user accepted the official Adafruit 3263 Eagle geometry as the documented basis for these visually identical modules. The schematic and PCB now implement J30/J31 on a shared SPI bus with separate CS and DRDY signals; FLT and `3Vo` are NC, and VIN is connected to +3V3. Both connectors now use `SSW-109-01-T-S` and the project footprint `Connector_Sirokko_Module:Samtec_SSW-109-01-T-S_1x09_P2.54mm_D1.04mm`; all 18 drills are Ø1.04 mm. Final fasteners, full 3D/mechanical acceptance, BOM and CAM remain unfinished.

The display, local Start/Stop button, rotary encoder, their dedicated circuitry, PCB footprints, copper and display mounting holes are removed by the latest direct requirement. Six released ESP pins are marked NC. Control, settings and diagnostics are now required only through the ESP32 access point and browser panel; the firmware is not yet implemented and remains on HOLD.

The current revision keeps K1 for the glow plug and K2 for the clutch. K3 and R703 are removed; K2.87 connects directly to J28.1. GPIO21/MCU_PERMIT is now NC, while GPIO18 remains NC. The change and its safety limitation are recorded in [`hardware/kicad/SIROKKO_TWO_RELAYS_CHANGE_RU.md`](hardware/kicad/SIROKKO_TWO_RELAYS_CHANGE_RU.md).

The first accepted general-relayout step moves J29 from `(324.00, 164.76)` to `(115.00, 130.00)` mm without changing its angle, pin numbering or nets. Only `/FAN_REQUEST` and `/SPEED_CMD` were rerouted; the board-wide via count fell from 210 to 188. The outline and two-layer stack are unchanged, and the general relayout is not complete.

The next accepted step rerouted only `/ROOM_DQ`: 15→3 vias and 281.554→234.503 mm of segments. All J29 changes remain; component positions, schematic, libraries and rules are unchanged.

The following accepted step rerouted only the relay command signals `/FUEL_CMD` and `/GLOW_CMD`. Their via counts fell from 8→2 and 8→4, and their segment lengths from 187.338→182.535 mm and 227.975→180.974 mm respectively. `/P24_PRESENT` is preserved exactly from the preceding accepted PCB after an attempted alternative route was rejected.

The current accepted step moves the 10 kΩ `/TC_AIR_CS` pull-up R3 from `(165, 85)` mm to `(110, 36.95)` mm near the ESP32. The main CS route is preserved; the branch now has 1 via instead of 5 and 101.795 mm of segments instead of 208.938 mm. The whole board now has 162 vias, 48 fewer than the original modular baseline; the general relayout is still incomplete.

Fresh KiCad 10.0.8 checks report ERC=0 and DRC=0 with 0 unconnected items and 0 schematic-parity differences. DRC has `ignored_checks=[]`; ERC retains four named standard ignored classes and adds none. The PCB has 78 footprints, 308 pads, 692 track segments, 162 vias and 32 zones: 854 track-and-via objects total. All 1906 native physical pad pairs are preserved; the two negative controls lose 11 and 13 required pairs. The current change and preservation checks are recorded in [`docs/research/MAX31856_CS_PULLUP_LAYOUT_RU.md`](docs/research/MAX31856_CS_PULLUP_LAYOUT_RU.md), with machine results in [`docs/research/tc_cs/`](docs/research/tc_cs/). The relay-command, ROOM_DQ and HC160 reports remain the history of preceding accepted steps. These are CAD checks, not electrical, thermal, mechanical or manufacturing acceptance.

Current C104 is Murata `GRM1885C1H222JA01D`, 2.2 nF, 50 V, C0G, ±5%. The accepted change and its limits are documented in [`docs/research/C104_BOOTSTRAP_REVIEW_RU.md`](docs/research/C104_BOOTSTRAP_REVIEW_RU.md); it is not a physical qualification or manufacturing approval.

The preceding discrete draft connects each MAX31856 BIAS output to T− and makes C9/C14 the 0.1 µF differential capacitors between T− and T+. Its source-grounded review remains [`docs/research/THERMOCOUPLE_BIAS_CORRECTION_RU.md`](docs/research/THERMOCOUPLE_BIAS_CORRECTION_RU.md). The exact schematic replacement is recorded in [`docs/research/MAX31856_MODULE_SCHEMATIC_INTEGRATION_RU.md`](docs/research/MAX31856_MODULE_SCHEMATIC_INTEGRATION_RU.md), and the socket application is recorded in [`docs/research/MAX31856_SOCKET_PCB_APPLIED_RU.md`](docs/research/MAX31856_SOCKET_PCB_APPLIED_RU.md). The existing five-sheet PDF predates the socket footprint/MPN assignment and is only a schematic overview, not an exact-current export or manufacturing artifact.

## Current PCB views — WIP / DRAFT

Front view (not a 3D render or assembly drawing):

![Sirokko modular PCB front view — WIP / DRAFT](docs/board-front-module-wip.png)

Current B.Cu view in board coordinates, not mirrored; it is not a 3D render or assembly drawing:

![Sirokko modular PCB back view — WIP / DRAFT](docs/board-back-module-wip.png)

Current documents:

- [`docs/REQUIREMENTS_RU.md`](docs/REQUIREMENTS_RU.md) — current product requirements;
- [`docs/CURRENT_STATE_RU.md`](docs/CURRENT_STATE_RU.md) — current state and limitations;
- [`hardware/kicad/REMOTE_ONLY_REQUIREMENTS_RU.md`](hardware/kicad/REMOTE_ONLY_REQUIREMENTS_RU.md) — remote-only control contract;
- [`hardware/kicad/ELECTRICAL_INTERFACE_RU.md`](hardware/kicad/ELECTRICAL_INTERFACE_RU.md) — verified connection table, relay logic and interface limitations;
- [`hardware/kicad/YD_V14_ELECTRICAL_INTEGRATION_RU.md`](hardware/kicad/YD_V14_ELECTRICAL_INTEGRATION_RU.md) — verified YD V1.4 power-path integration notes and unresolved qualification limits;
- [`hardware/kicad/YD_3V3_LOAD_BUDGET_RU.md`](hardware/kicad/YD_3V3_LOAD_BUDGET_RU.md) and [`hardware/kicad/YD_3V3_LOAD_BUDGET.json`](hardware/kicad/YD_3V3_LOAD_BUDGET.json) — preceding discrete 3.3 V load calculation; it must be recalculated for the exact modules before release;
- [`docs/research/C104_BOOTSTRAP_REVIEW_RU.md`](docs/research/C104_BOOTSTRAP_REVIEW_RU.md) — accepted C104 C0G selection and its calculation and application limits;
- [`docs/research/RELAY_DG85BM_COIL_ENVELOPE_REVIEW_RU.md`](docs/research/RELAY_DG85BM_COIL_ENVELOPE_REVIEW_RU.md) and [`docs/research/RELAY_DG85BM_COIL_ENVELOPE_REVIEW.json`](docs/research/RELAY_DG85BM_COIL_ENVELOPE_REVIEW.json) — current two-relay coil, driver and hot-pickup calculation;
- [`docs/research/MAX31856_MODULE_PCB_INTEGRATION_RU.md`](docs/research/MAX31856_MODULE_PCB_INTEGRATION_RU.md) — preceding modular PCB routing, DRC/ERC and connectivity evidence;
- [`docs/research/MAX31856_CS_PULLUP_LAYOUT_RU.md`](docs/research/MAX31856_CS_PULLUP_LAYOUT_RU.md) and [`docs/research/tc_cs/`](docs/research/tc_cs/) — current R3 relocation and TC_AIR_CS simplification, exact hashes, DRC/ERC and preservation checks;
- [`docs/research/RELAY_COMMAND_ROUTES_APPLIED_RU.md`](docs/research/RELAY_COMMAND_ROUTES_APPLIED_RU.md) and [`docs/research/logic/`](docs/research/logic/) — historical acceptance of the preceding FUEL_CMD/GLOW_CMD reroute;
- [`docs/research/DS18B20_ROUTE_APPLIED_RU.md`](docs/research/DS18B20_ROUTE_APPLIED_RU.md) and [`docs/research/room/`](docs/research/room/) — historical acceptance of the preceding ROOM_DQ reroute;
- [`docs/research/HC160_LAYOUT_APPLIED_RU.md`](docs/research/HC160_LAYOUT_APPLIED_RU.md) and [`docs/research/relayout/`](docs/research/relayout/) — historical acceptance of the J29-relayout step;
- [`docs/research/MAX31856_SOCKET_PCB_APPLIED_RU.md`](docs/research/MAX31856_SOCKET_PCB_APPLIED_RU.md) — historical J30/J31 socket acceptance before the current relayout;
- [`docs/BOM_DRAFT_NOT_FOR_ORDER.csv`](docs/BOM_DRAFT_NOT_FOR_ORDER.csv) — KiCad CSV BOM with MPN for 77 schematic instances in the preceding discrete snapshot; it does not match the current modular PCB and is not orderable;
- [`hardware/kicad/PCB_REVIEW_RU.md`](hardware/kicad/PCB_REVIEW_RU.md) — preceding modular PCB verification retained as history;
- [`hardware/kicad/README.md`](hardware/kicad/README.md) — project entry points and local dependencies;
- [`manufacturing/DRAFT_NOT_FOR_ORDER/`](manufacturing/DRAFT_NOT_FOR_ORDER/) — preliminary Gerber, drill, BOM and placement outputs for the preceding discrete snapshot; **DRAFT / NOT FOR ORDER / NO POWER / NO FLASH / NO FIRE**.

There is no approved firmware for this hardware contract. Firmware work remains blocked until the final remote-control contract and hardware acceptance are complete. Electrical and thermal acceptance and the manufacturing release remain unfinished; the preliminary manufacturing outputs above are not a release or permission to order, assemble, power, flash or operate the board.

Revision copies, verification reports, renders, snapshots and archives are not kept beside the sources. Normal Git history is the only recovery path for earlier states.
