# Sirokko 262.01 controller

> **DRAFT / NOT FOR ORDER / NO POWER / NO FLASH / NO FIRE.** The board is unfinished, has no manufacturing release, and has not passed physical acceptance. Do not order it, power it, flash it, or connect it to a burner.

The working tree contains one current KiCad project:

- project: [`hardware/kicad/Sirokko26201_control_motor_candidate.kicad_pro`](hardware/kicad/Sirokko26201_control_motor_candidate.kicad_pro);
- schematic: [`hardware/kicad/Sirokko26201_control_motor_candidate.kicad_sch`](hardware/kicad/Sirokko26201_control_motor_candidate.kicad_sch);
- PCB: [`hardware/kicad/Sirokko26201_control_motor_candidate.kicad_pcb`](hardware/kicad/Sirokko26201_control_motor_candidate.kicad_pcb);
- current schematic SHA-256: `8857489e2cb666875fa123cc92b904645efd90dff04d8d8b168e6403f9691649`;
- current PCB SHA-256: `1905414e7b0e475155571546124c7522033b31af004a7bede8ba72e2ff1d1517`;
- current power-sheet SHA-256: `36248b3d5b1e4c93b9551ad1ab19b3abd59f357226a311998b310b7e8ac1514d`.

The display, local Start/Stop button, rotary encoder, their dedicated circuitry, PCB footprints, copper and display mounting holes are removed by the latest direct requirement. Six released ESP pins are marked NC. The main PCB is synchronized with the current schematic. Control, settings and diagnostics are now required only through a software remote interface implemented by the ESP32; the protocol and firmware are not yet accepted or implemented.

The current revision keeps K1 for the glow plug and K2 for the clutch. K3 and R703 are removed; K2.87 connects directly to J28.1. GPIO21/MCU_PERMIT is now NC, while GPIO18 remains NC. The change and its safety limitation are recorded in [`hardware/kicad/SIROKKO_TWO_RELAYS_CHANGE_RU.md`](hardware/kicad/SIROKKO_TWO_RELAYS_CHANGE_RU.md).

Fresh KiCad 10.0.7 checks report ERC=0 and DRC=0 violations, 0 unconnected items and 0 PCB↔schematic parity differences; DRC has `ignored_checks=[]`, while the four standard ERC ignored checks are listed in the report. The board has 86 footprints, 338 pads, 861 track segments and 253 vias. These are CAD checks, not electrical, thermal or manufacturing acceptance.

Current C104 is Murata `GRM1885C1H222JA01D`, 2.2 nF, 50 V, C0G, ±5%. The accepted change and its limits are documented in [`docs/research/C104_BOOTSTRAP_REVIEW_RU.md`](docs/research/C104_BOOTSTRAP_REVIEW_RU.md); it is not a physical qualification or manufacturing approval.

## Current PCB views — WIP / DRAFT

Front view (not a 3D render or assembly drawing):

![Sirokko PCB front view — WIP / DRAFT](docs/board-front.png)

Back view (not a 3D render or assembly drawing):

![Sirokko PCB back view — WIP / DRAFT](docs/board-back.png)

Current documents:

- [`docs/REQUIREMENTS_RU.md`](docs/REQUIREMENTS_RU.md) — current product requirements;
- [`docs/CURRENT_STATE_RU.md`](docs/CURRENT_STATE_RU.md) — current state and limitations;
- [`hardware/kicad/REMOTE_ONLY_REQUIREMENTS_RU.md`](hardware/kicad/REMOTE_ONLY_REQUIREMENTS_RU.md) — remote-only control contract;
- [`hardware/kicad/ELECTRICAL_INTERFACE_RU.md`](hardware/kicad/ELECTRICAL_INTERFACE_RU.md) — verified connection table, relay logic and interface limitations;
- [`hardware/kicad/YD_V14_ELECTRICAL_INTEGRATION_RU.md`](hardware/kicad/YD_V14_ELECTRICAL_INTEGRATION_RU.md) — verified YD V1.4 power-path integration notes and unresolved qualification limits;
- [`hardware/kicad/YD_3V3_LOAD_BUDGET_RU.md`](hardware/kicad/YD_3V3_LOAD_BUDGET_RU.md) and [`hardware/kicad/YD_3V3_LOAD_BUDGET.json`](hardware/kicad/YD_3V3_LOAD_BUDGET.json) — current partial 3.3 V load budget and its machine-readable source;
- [`docs/research/C104_BOOTSTRAP_REVIEW_RU.md`](docs/research/C104_BOOTSTRAP_REVIEW_RU.md) — accepted C104 C0G selection and its calculation and application limits;
- [`docs/research/RELAY_DG85BM_COIL_ENVELOPE_REVIEW_RU.md`](docs/research/RELAY_DG85BM_COIL_ENVELOPE_REVIEW_RU.md) and [`docs/research/RELAY_DG85BM_COIL_ENVELOPE_REVIEW.json`](docs/research/RELAY_DG85BM_COIL_ENVELOPE_REVIEW.json) — current two-relay coil, driver and hot-pickup calculation;
- [`docs/BOM_DRAFT_NOT_FOR_ORDER.csv`](docs/BOM_DRAFT_NOT_FOR_ORDER.csv) — KiCad CSV BOM with MPN for 77 schematic instances; draft, not an orderable procurement BOM;
- [`hardware/kicad/PCB_REVIEW_RU.md`](hardware/kicad/PCB_REVIEW_RU.md) — current PCB verification and remaining acceptance work;
- [`hardware/kicad/README.md`](hardware/kicad/README.md) — project entry points and local dependencies;
- [`manufacturing/DRAFT_NOT_FOR_ORDER/`](manufacturing/DRAFT_NOT_FOR_ORDER/) — preliminary Gerber, drill, BOM and placement outputs; **DRAFT / NOT FOR ORDER / NO POWER / NO FLASH / NO FIRE**.

There is no approved firmware for this hardware contract. Firmware work remains blocked until the final remote-control contract and hardware acceptance are complete. Electrical and thermal acceptance and the manufacturing release remain unfinished; the preliminary manufacturing outputs above are not a release or permission to order, assemble, power, flash or operate the board.

Revision copies, verification reports, renders, snapshots and archives are not kept beside the sources. Normal Git history is the only recovery path for earlier states.
