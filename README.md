# Sirokko 262.01 controller

> **DRAFT / NOT FOR ORDER / NO POWER / NO FLASH / NO FIRE.** The board is unfinished, has no manufacturing release, and has not passed physical acceptance. Do not order it, power it, flash it, or connect it to a burner.

The working tree contains one current KiCad project:

- project: [`hardware/kicad/Sirokko26201_control_motor_candidate.kicad_pro`](hardware/kicad/Sirokko26201_control_motor_candidate.kicad_pro);
- schematic: [`hardware/kicad/Sirokko26201_control_motor_candidate.kicad_sch`](hardware/kicad/Sirokko26201_control_motor_candidate.kicad_sch);
- PCB: [`hardware/kicad/Sirokko26201_control_motor_candidate.kicad_pcb`](hardware/kicad/Sirokko26201_control_motor_candidate.kicad_pcb);
- current schematic SHA-256: `229717825a8363ef08bf17ac713fe12f769c21b18ce333c26ee919302d7cc628`;
- unchanged PCB SHA-256: `b11c63de29efd64ce23d33bcd5e74f7ddecd919ec385467222ebf0c1102e6cf3`.

The display, local Start/Stop button and rotary encoder are removed from the current schematic by the latest direct requirement. Six released ESP pins are marked NC. Control, settings and diagnostics are now required only through a software remote interface implemented by the ESP32. The protocol and the firmware implementation are not yet accepted or implemented.

**Only the schematic has been updated. The PCB is deliberately unchanged and still contains the obsolete display and local-control placement, copper and mounting geometry; it does not match the current schematic.** Do not run F8, update the PCB from the schematic, refill zones, or treat this publication as removal of those parts from the PCB. The fresh KiCad 10.0.7 ERC result is zero violations, but that is not PCB acceptance or a manufacturing release.

Current documents:

- [`README_RU.md`](README_RU.md) — primary Russian entry point;
- [`docs/REQUIREMENTS_RU.md`](docs/REQUIREMENTS_RU.md) — current product requirements;
- [`docs/CURRENT_STATE_RU.md`](docs/CURRENT_STATE_RU.md) — current state and limitations;
- [`hardware/kicad/REMOTE_ONLY_REQUIREMENTS_RU.md`](hardware/kicad/REMOTE_ONLY_REQUIREMENTS_RU.md) — remote-only control contract;
- [`hardware/kicad/README.md`](hardware/kicad/README.md) — project entry points and local dependencies.

There is no approved firmware for this hardware contract. Firmware work remains blocked until the PCB is updated and accepted against the schematic and final pin map.

Revision copies, verification reports, renders, snapshots and archives are not kept beside the sources. Normal Git history is the only recovery path for earlier states.
