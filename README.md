# Sirokko 262.01 controller

> **DRAFT / NOT FOR ORDER / NO POWER / NO FLASH / NO FIRE.** The board is unfinished, has no manufacturing release, and has not passed physical acceptance. Do not order it, power it, flash it, or connect it to a burner.

The working tree contains one current KiCad project:

- project: [`hardware/kicad/Sirokko26201_control_motor_candidate.kicad_pro`](hardware/kicad/Sirokko26201_control_motor_candidate.kicad_pro);
- schematic: [`hardware/kicad/Sirokko26201_control_motor_candidate.kicad_sch`](hardware/kicad/Sirokko26201_control_motor_candidate.kicad_sch);
- PCB: [`hardware/kicad/Sirokko26201_control_motor_candidate.kicad_pcb`](hardware/kicad/Sirokko26201_control_motor_candidate.kicad_pcb);
- HC-160A-S2 interface: [`hardware/kicad/HC160_INTERFACE_RU.md`](hardware/kicad/HC160_INTERFACE_RU.md);
- current PCB SHA-256: `b11c63de29efd64ce23d33bcd5e74f7ddecd919ec385467222ebf0c1102e6cf3`.

The PCB contains 117 footprints, 439 physical pads, 1,357 segments and 419 vias. The former on-board motor H-bridge and its 28 components are removed. HC-160A-S2 is an external module and is not mounted on this PCB; both motor wires and its power wiring go directly to that module. Sirokko connector J29 carries control only: `1=A1/FAN_REQUEST`, `2=A2/GND`, `3=PA/SPEED_CMD`, `4=G/GND`. The module's `+5V OUT` is unused. `J1.11/GPIO18` is NC.

A read-only KiCad 10.0.7 check of the paired project exported the schematic netlist and reported zero ERC violations, zero unconnected items, zero schematic-parity issues and zero DRC error-level violations. Whole-board DRC still exits with code 5 because 51 warnings remain: 46 `track_dangling` and five `copper_sliver`. This is not a clean DRC pass or manufacturing acceptance.

There is no approved firmware for this hardware contract. Earlier firmware that drives GPIO18 or assumes the removed on-board motor stage must not be flashed. Firmware reconciliation starts only after the PCB and final pin map are accepted.

The previously considered Waveshare 18366 display is no longer the selected display. A replacement is still being chosen; 12219 is only a candidate, and the final mounting interface is not confirmed. Existing display geometry in this WIP snapshot is not a manufacturing decision.

Current documents:

- [`README_RU.md`](README_RU.md) — primary Russian entry point;
- [`docs/REQUIREMENTS_RU.md`](docs/REQUIREMENTS_RU.md) — requirements;
- [`docs/CURRENT_STATE_RU.md`](docs/CURRENT_STATE_RU.md) — current state and limitations;
- [`hardware/kicad/README.md`](hardware/kicad/README.md) — project entry points and local dependencies.

Revision copies, verification reports, renders, snapshots and archives are not kept beside the sources. Normal Git history is the only recovery path for earlier states.
