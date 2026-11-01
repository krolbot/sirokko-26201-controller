# Sirokko 262.01 controller

> **DRAFT / REVIEW REQUIRED / NOT FOR ORDER / NO POWER / NO FLASH / NO FIRE; firmware HOLD.** The board is unfinished and has no manufacturing release. Do not order it, power it, flash it, or connect it to a burner. The canonical schematic and PCB now omit the separate 24 V-presence detector (`U20/Q20/D20/R120–R123/C28`); `J1.6/GPIO6` is NC. `R108/C109` remain schematic-only WIP and are the only expected SCH↔PCB mismatch.

The [current review package](manufacturing/REVIEW_REQUIRED_NO_P24_2026-11-01/) contains the exact canonical KiCad sources, BOM, Gerber, Excellon, positions, assembly PDF/PNG, STEP, hashes and audits. KiCad 10.0.8 reports DRC=0, ERC=0, no unconnected PCB items and `validate_board=ok`. It remains `REVIEW REQUIRED` and is not orderable.

The current input-protection acceptance is documented in [`docs/research/INPUT_PROTECTION_ACCEPTANCE_RU.md`](docs/research/INPUT_PROTECTION_ACCEPTANCE_RU.md), with machine-readable checks in [`docs/research/INPUT_PROTECTION_ACCEPTANCE.json`](docs/research/INPUT_PROTECTION_ACCEPTANCE.json). The pin map and selected copper connectivity passed; static ±28.8 V reverse polarity passed absolute-limit consistency only, while negative-pulse qualification with a charged output remains open (71.3 V calculated across a 70 V pin-to-pin limit). No PCB geometry or firmware changed, and this is not a functional, physical or manufacturing acceptance.

The current startup model is documented in [`docs/research/INPUT_STARTUP_MODEL_RU.md`](docs/research/INPUT_STARTUP_MODEL_RU.md), with reproducible arithmetic in [`docs/research/INPUT_STARTUP_MODEL.json`](docs/research/INPUT_STARTUP_MODEL.json). The schematic candidate uses R108=22 kΩ and C109=33 nF; the earlier 12 nF trial is cancelled. Its 17.28 ms idealized ramp at typical `IHGATE=55 µA` is conditional, not a measurement, startup guarantee or SOA pass. Status: `STARTUP_NOT_QUALIFIED`; negative-surge coordination remains open. The branch is schematic-only and is not routed on the PCB.

The working tree contains one intentionally incomplete KiCad WIP:

- project: [`hardware/kicad/Sirokko26201_control_motor_candidate.kicad_pro`](hardware/kicad/Sirokko26201_control_motor_candidate.kicad_pro);
- schematic: [`hardware/kicad/Sirokko26201_control_motor_candidate.kicad_sch`](hardware/kicad/Sirokko26201_control_motor_candidate.kicad_sch);
- PCB: [`hardware/kicad/Sirokko26201_control_motor_candidate.kicad_pcb`](hardware/kicad/Sirokko26201_control_motor_candidate.kicad_pcb);
- current root schematic SHA-256: `6c62c1d9fc13e621cfff8c6be2f9548082b5b4e7aaf046d7d7f454c1077fc76a`;
- current input-protection sheet SHA-256: `334a081f26003351fcd2a1ead5d2d6be32adb3dded125e289fbb2d50c8b3cfde`;
- current PCB SHA-256: `55f7bc9ab78f482ab523208c198f7178c71b29f96c9c99016b4d6e50e4fa0384`;
- current `fp-lib-table` SHA-256: `8e9cf2caf9090ad7d5e78625f981dc0fe8f22895f48515eb55261a27f1c247ac`;
- current power-sheet SHA-256: `36248b3d5b1e4c93b9551ad1ab19b3abd59f357226a311998b310b7e8ac1514d`.

The latest product decision selects two removable blue MAX31856 modules from <https://ozon.ru/t/3QtIbaX> instead of the on-board U9/U10 circuits. The exact module is not MAX31865. The user accepted the official Adafruit 3263 Eagle geometry as the documented basis for these visually identical modules. The schematic and PCB now implement J30/J31 on a shared SPI bus with separate CS and DRDY signals; FLT and `3Vo` are NC, and VIN is connected to +3V3. Both connectors now use `SSW-109-01-T-S` and the project footprint `Connector_Sirokko_Module:Samtec_SSW-109-01-T-S_1x09_P2.54mm_D1.04mm`; all 18 drills are Ø1.04 mm. The modular BOM is published only as a draft: final fasteners, exact external variants, procurement readiness, full 3D/mechanical acceptance and current CAM remain unfinished.

The display, local Start/Stop button, rotary encoder, their dedicated circuitry, PCB footprints, copper and display mounting holes are removed by the latest direct requirement. Six released ESP pins are marked NC. Control, settings and diagnostics are now required only through the ESP32 access point and browser panel; the firmware is not yet implemented and remains on HOLD.

The current revision keeps K1 for the glow plug and K2 for the clutch. K3 and R703 are removed; K2.87 connects directly to J28.1. GPIO21/MCU_PERMIT is now NC, while GPIO18 remains NC. The change and its safety limitation are recorded in [`hardware/kicad/SIROKKO_TWO_RELAYS_CHANGE_RU.md`](hardware/kicad/SIROKKO_TWO_RELAYS_CHANGE_RU.md).

The first accepted general-relayout step moves J29 from `(324.00, 164.76)` to `(115.00, 130.00)` mm without changing its angle, pin numbering or nets. Only `/FAN_REQUEST` and `/SPEED_CMD` were rerouted; the board-wide via count fell from 210 to 188. The outline and two-layer stack are unchanged, and the general relayout is not complete.

The next accepted step rerouted only `/ROOM_DQ`: 15→3 vias and 281.554→234.503 mm of segments. All J29 changes remain; component positions, schematic, libraries and rules are unchanged.

The following historical step rerouted only the relay command signals `/FUEL_CMD` and `/GLOW_CMD`. Its preservation statement predates and is superseded by the current removal of the 24 V-presence detector.

The 10 kΩ `/TC_AIR_CS` pull-up R3 remains at `(110, 36.95)` mm near the ESP32. After removal of the 24 V-presence detector, the current PCB contains 139 vias; the general relayout is still incomplete.

Fresh KiCad 10.0.8 checks on the current sources report ERC=0, DRC=0, no unconnected PCB items and `validate_board=ok`. The native audit confirms exact removal of the eight target footprints and their dedicated copper; the current cleanup additionally removes only three fully duplicated segments while preserving all 287 pads, all 1600 physical pairs and every other copper object exactly. Negative controls reject both a restored target ref/net and a broken retained connection. R108/C109 are not routed. These are CAD checks, not electrical, thermal, mechanical or manufacturing acceptance.

Current C104 is Murata `GRM1885C1H222JA01D`, 2.2 nF, 50 V, C0G, ±5%. The accepted change and its limits are documented in [`docs/research/C104_BOOTSTRAP_REVIEW_RU.md`](docs/research/C104_BOOTSTRAP_REVIEW_RU.md); it is not a physical qualification or manufacturing approval.

The preceding discrete draft connects each MAX31856 BIAS output to T− and makes C9/C14 the 0.1 µF differential capacitors between T− and T+. Its source-grounded review remains [`docs/research/THERMOCOUPLE_BIAS_CORRECTION_RU.md`](docs/research/THERMOCOUPLE_BIAS_CORRECTION_RU.md). The exact schematic replacement is recorded in [`docs/research/MAX31856_MODULE_SCHEMATIC_INTEGRATION_RU.md`](docs/research/MAX31856_MODULE_SCHEMATIC_INTEGRATION_RU.md), and the socket application is recorded in [`docs/research/MAX31856_SOCKET_PCB_APPLIED_RU.md`](docs/research/MAX31856_SOCKET_PCB_APPLIED_RU.md). The existing five-sheet PDF predates the socket footprint/MPN assignment and is only a schematic overview, not an exact-current export or manufacturing artifact.

## Current PCB views — WIP / DRAFT

Front view (not a 3D render or assembly drawing):

![Sirokko modular PCB front view — WIP / DRAFT](docs/board-front-module-wip.png)

Current mirrored back assembly view; it is not a 3D render:

![Sirokko modular PCB back view — WIP / DRAFT](docs/board-back-module-wip.png)

Current documents:

- [`docs/REQUIREMENTS_RU.md`](docs/REQUIREMENTS_RU.md) — current product requirements;
- [`docs/HISTORICAL_P24_RETIREMENT_RU.md`](docs/HISTORICAL_P24_RETIREMENT_RU.md) — freezes prior P24/GPIO6 reports as non-current evidence;
- [`docs/CURRENT_STATE_RU.md`](docs/CURRENT_STATE_RU.md) — current state and limitations;
- [`hardware/kicad/REMOTE_ONLY_REQUIREMENTS_RU.md`](hardware/kicad/REMOTE_ONLY_REQUIREMENTS_RU.md) — remote-only control contract;
- [`hardware/kicad/ELECTRICAL_INTERFACE_RU.md`](hardware/kicad/ELECTRICAL_INTERFACE_RU.md) — verified connection table, relay logic and interface limitations;
- [`hardware/kicad/YD_V14_ELECTRICAL_INTEGRATION_RU.md`](hardware/kicad/YD_V14_ELECTRICAL_INTEGRATION_RU.md) — verified YD V1.4 power-path integration notes and unresolved qualification limits;
- [`hardware/kicad/YD_3V3_LOAD_BUDGET_RU.md`](hardware/kicad/YD_3V3_LOAD_BUDGET_RU.md) and [`hardware/kicad/YD_3V3_LOAD_BUDGET.json`](hardware/kicad/YD_3V3_LOAD_BUDGET.json) — preceding discrete 3.3 V load calculation; it must be recalculated for the exact modules before release;
- [`docs/research/C104_BOOTSTRAP_REVIEW_RU.md`](docs/research/C104_BOOTSTRAP_REVIEW_RU.md) — accepted C104 C0G selection and its calculation and application limits;
- [`docs/research/CONTROL_INTERFACE_ACCEPTANCE_RU.md`](docs/research/CONTROL_INTERFACE_ACCEPTANCE_RU.md) and [`docs/research/CONTROL_INTERFACE_ACCEPTANCE.json`](docs/research/CONTROL_INTERFACE_ACCEPTANCE.json) — current 75-pin/30-connection control and sensor topology check; not functional qualification;
- [`docs/research/INPUT_STARTUP_MODEL_RU.md`](docs/research/INPUT_STARTUP_MODEL_RU.md) and [`docs/research/INPUT_STARTUP_MODEL.json`](docs/research/INPUT_STARTUP_MODEL.json) — the 33 nF/22 kΩ schematic WIP, conditional startup model and explicit `STARTUP_NOT_QUALIFIED` status;
- [`docs/research/RELAY_DG85BM_COIL_ENVELOPE_REVIEW_RU.md`](docs/research/RELAY_DG85BM_COIL_ENVELOPE_REVIEW_RU.md) and [`docs/research/RELAY_DG85BM_COIL_ENVELOPE_REVIEW.json`](docs/research/RELAY_DG85BM_COIL_ENVELOPE_REVIEW.json) — current two-relay coil, driver and hot-pickup calculation;
- [`docs/research/MAX31856_MODULE_PCB_INTEGRATION_RU.md`](docs/research/MAX31856_MODULE_PCB_INTEGRATION_RU.md) — preceding modular PCB routing, DRC/ERC and connectivity evidence;
- [`docs/research/MAX31856_CS_PULLUP_LAYOUT_RU.md`](docs/research/MAX31856_CS_PULLUP_LAYOUT_RU.md) and [`docs/research/tc_cs/`](docs/research/tc_cs/) — current R3 relocation and TC_AIR_CS simplification, exact hashes, DRC/ERC and preservation checks;
- [`docs/research/RELAY_COMMAND_ROUTES_APPLIED_RU.md`](docs/research/RELAY_COMMAND_ROUTES_APPLIED_RU.md) and [`docs/research/logic/`](docs/research/logic/) — historical acceptance of the preceding FUEL_CMD/GLOW_CMD reroute;
- [`docs/research/DS18B20_ROUTE_APPLIED_RU.md`](docs/research/DS18B20_ROUTE_APPLIED_RU.md) and [`docs/research/room/`](docs/research/room/) — historical acceptance of the preceding ROOM_DQ reroute;
- [`docs/research/HC160_LAYOUT_APPLIED_RU.md`](docs/research/HC160_LAYOUT_APPLIED_RU.md) and [`docs/research/relayout/`](docs/research/relayout/) — historical acceptance of the J29-relayout step;
- [`docs/research/MAX31856_SOCKET_PCB_APPLIED_RU.md`](docs/research/MAX31856_SOCKET_PCB_APPLIED_RU.md) — historical J30/J31 socket acceptance before the current relayout;
- [`docs/BOM_PURCHASE_NOTES_RU.md`](docs/BOM_PURCHASE_NOTES_RU.md) — the current BOM index: 59 schematic references, 38 grouped PCB purchasing rows and 60 physical PCB items; R108/C109 are schematic-only, external modules/sensors and mechanical interfaces are separate, and none of these files is order approval;
- [`hardware/kicad/PCB_REVIEW_RU.md`](hardware/kicad/PCB_REVIEW_RU.md) — preceding modular PCB verification retained as history;
- [`hardware/kicad/README.md`](hardware/kicad/README.md) — project entry points and local dependencies;
- [`manufacturing/REVIEW_REQUIRED_NO_P24_2026-11-01/SIROKKO_NO_P24_2026-11-01_REVIEW_REQUIRED.zip`](manufacturing/REVIEW_REQUIRED_NO_P24_2026-11-01/SIROKKO_NO_P24_2026-11-01_REVIEW_REQUIRED.zip) — the single current source/CAM/BOM package for PCB `55f7bc9a…0384`; **REVIEW REQUIRED / NOT FOR ORDER / NO POWER / NO FLASH / NO FIRE**, not a manufacturing release.

There is no approved firmware for this hardware contract. Firmware work remains blocked until the final remote-control contract and hardware acceptance are complete. Electrical and thermal acceptance and the manufacturing release remain unfinished; the preliminary manufacturing outputs above are not a release or permission to order, assemble, power, flash or operate the board.

Revision copies, verification reports, renders, snapshots and archives are not kept beside the sources. Normal Git history is the only recovery path for earlier states.
