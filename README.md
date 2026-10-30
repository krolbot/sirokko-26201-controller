# Sirokko 262.01 controller

> **DRAFT / NOT FOR ORDER / NO POWER / NO FLASH / NO FIRE.** The board is unfinished and has not passed manufacturing or physical acceptance. Do not order it, power it, flash it, or connect it to a burner.

The working tree contains one current KiCad project:

- project: [`hardware/kicad/Sirokko26201_control_motor_candidate.kicad_pro`](hardware/kicad/Sirokko26201_control_motor_candidate.kicad_pro);
- schematic: [`hardware/kicad/Sirokko26201_control_motor_candidate.kicad_sch`](hardware/kicad/Sirokko26201_control_motor_candidate.kicad_sch);
- PCB: [`hardware/kicad/Sirokko26201_control_motor_candidate.kicad_pcb`](hardware/kicad/Sirokko26201_control_motor_candidate.kicad_pcb);
- current PCB SHA-256: `0f29b0eb52f23cd80000c642781b561ff389ced245c62f7621cfe1a4008e463f`.

The current PCB contains 144 footprints, 115 nets, 1,535 segments, 444 vias and 35 zones. `GPIO18 = J1.11 = /OCP_OK`: a high-impedance digital input where LOW reports a hardware overcurrent-cutoff event. The MCU must never drive the net or enable an internal pull-down. R9008 is the existing external pull-up to +3V3; no second pull-up is required. The hardware cutoff path through U9003 and U9005 remains independent of the MCU. J1.3 remains `unconnected-(J1-Pin_3-Pad3)`; the DevKit's internal EN circuit was not changed.

The accepted KiCad 10.0.7 readback reported zero ERC violations and a non-passing whole-board DRC with 58 warnings and 17 unconnected items. The OCP change added no shorts, clearance violations or opens. This is unfinished board state, not manufacturing acceptance.

There is no firmware in the working tree. Future firmware must treat `/OCP_OK` as read-only, latch the fault in software, inhibit ignition and automatic restart, and disable FUEL/GLOW. Minimum OCP pulse width, GPIO capture and end-to-end reaction time still require a separate verification; purge must not be promised when the motor has failed.

Current documents:

- [`README_RU.md`](README_RU.md) — primary Russian entry point;
- [`docs/REQUIREMENTS_RU.md`](docs/REQUIREMENTS_RU.md) — requirements;
- [`docs/CURRENT_STATE_RU.md`](docs/CURRENT_STATE_RU.md) — current state and limitations;
- [`docs/research/YD_V14_ELECTRICAL_INTEGRATION_RU.md`](docs/research/YD_V14_ELECTRICAL_INTEGRATION_RU.md) — active DevKit pin contract;
- [`hardware/kicad/README.md`](hardware/kicad/README.md) — project entry points and local dependencies.

Revision copies, verification reports, renders, snapshots and archives are not kept beside the sources. Normal Git history is the only recovery path for earlier states.
