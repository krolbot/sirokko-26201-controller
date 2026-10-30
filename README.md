# Sirokko 262.01 controller

> **DRAFT / NOT FOR ORDER / NO POWER / NO FLASH / NO FIRE.** The board is unfinished, has no manufacturing release, and has not passed physical acceptance. Do not order it, power it, flash it, or connect it to a burner.

The working tree contains one current KiCad project:

- project: [`hardware/kicad/Sirokko26201_control_motor_candidate.kicad_pro`](hardware/kicad/Sirokko26201_control_motor_candidate.kicad_pro);
- schematic: [`hardware/kicad/Sirokko26201_control_motor_candidate.kicad_sch`](hardware/kicad/Sirokko26201_control_motor_candidate.kicad_sch);
- PCB: [`hardware/kicad/Sirokko26201_control_motor_candidate.kicad_pcb`](hardware/kicad/Sirokko26201_control_motor_candidate.kicad_pcb);
- current schematic SHA-256: `229717825a8363ef08bf17ac713fe12f769c21b18ce333c26ee919302d7cc628`;
- current PCB SHA-256: `21c74b228efba46c59b170671db9498bd18c74b11999a4542b16d004a965c453`.

The display, local Start/Stop button, rotary encoder, their dedicated circuitry, PCB footprints, copper and display mounting holes are removed by the latest direct requirement. Six released ESP pins are marked NC. The main PCB is synchronized with the current schematic. Control, settings and diagnostics are now required only through a software remote interface implemented by the ESP32; the protocol and firmware are not yet accepted or implemented.

Fresh KiCad 10.0.7 checks report ERC=0 and DRC=0 errors, 0 warnings, 0 unconnected items and 0 PCB↔schematic parity differences. All 2703 previously connected physical pad pairs remain connected. The board is two-layer, with 347 pads, 897 track segments and 261 vias. These are CAD geometry and connectivity checks, not electrical, thermal or manufacturing acceptance. See [`hardware/kicad/PCB_REVIEW_RU.md`](hardware/kicad/PCB_REVIEW_RU.md) for the exact scope and limitations.

## Current PCB views — WIP / DRAFT

Front view (not a 3D render or assembly drawing):

![Sirokko PCB front view — WIP / DRAFT](docs/board-front.png)

Back view (not a 3D render or assembly drawing):

![Sirokko PCB back view — WIP / DRAFT](docs/board-back.png)

Current documents:

- [`docs/REQUIREMENTS_RU.md`](docs/REQUIREMENTS_RU.md) — current product requirements;
- [`docs/CURRENT_STATE_RU.md`](docs/CURRENT_STATE_RU.md) — current state and limitations;
- [`hardware/kicad/REMOTE_ONLY_REQUIREMENTS_RU.md`](hardware/kicad/REMOTE_ONLY_REQUIREMENTS_RU.md) — remote-only control contract;
- [`hardware/kicad/PCB_REVIEW_RU.md`](hardware/kicad/PCB_REVIEW_RU.md) — current PCB verification and remaining acceptance work;
- [`hardware/kicad/README.md`](hardware/kicad/README.md) — project entry points and local dependencies.

There is no approved firmware for this hardware contract. Firmware work remains blocked until the final remote-control contract and hardware acceptance are complete. Electrical and thermal acceptance, Gerber/drill/BOM/PnP outputs and the manufacturing release are also unfinished.

Revision copies, verification reports, renders, snapshots and archives are not kept beside the sources. Normal Git history is the only recovery path for earlier states.
