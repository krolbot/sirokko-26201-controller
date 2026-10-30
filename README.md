# Sirokko 262.01 controller

> **DRAFT / NOT FOR ORDER / NO POWER / NO FIRE.** The board is unfinished and has not passed manufacturing or physical acceptance. Do not order it, power it, or connect it to a burner.

The working tree contains one current, complete KiCad project:

- project: [`hardware/kicad/Sirokko26201_control_motor_candidate.kicad_pro`](hardware/kicad/Sirokko26201_control_motor_candidate.kicad_pro);
- schematic: [`hardware/kicad/Sirokko26201_control_motor_candidate.kicad_sch`](hardware/kicad/Sirokko26201_control_motor_candidate.kicad_sch);
- PCB: [`hardware/kicad/Sirokko26201_control_motor_candidate.kicad_pcb`](hardware/kicad/Sirokko26201_control_motor_candidate.kicad_pcb);
- current PCB SHA-256: `18c19f6415c74e5b2ec4e1d33481a216533cde11e6ebb5407d7605f8e5c79261`.

The current PCB contains 156 footprints, 591 physical pads, 1,523 segments and 448 vias. The separate RESET button, J24, R702 and `RESET_RELEASED` net were removed without a replacement header, jumper or hidden button. U6.4 is tied to `+3V3`; the watchdog, hardware latch, automatic reset and requirement for a new `ARM_PULSE` remain.

The latest native KiCad 10.0.7 check reported 82 DRC warnings, 54 unconnected items, 51 schematic-parity issues and 2 ERC warnings. This is unfinished board state, not manufacturing acceptance.

The revoked bare-TFT M2×12 fastener model has been removed. The official nominal Waveshare 18366 STEP remains, but the factory standoff thread and height, mating hardware and full Z-stack are not accepted.

There is no firmware in the working tree. The former bench implementation was rejected as a basis for the final product and is available only through Git history. Future firmware must not restore the old manual-RESET functions; arming uses `ARM_PULSE` only during an allowed start. No ILI9341 driver is implemented.

Current documents:

- [`README_RU.md`](README_RU.md) — primary Russian entry point;
- [`docs/REQUIREMENTS_RU.md`](docs/REQUIREMENTS_RU.md) — requirements;
- [`docs/CURRENT_STATE_RU.md`](docs/CURRENT_STATE_RU.md) — current state and limitations;
- [`hardware/kicad/README.md`](hardware/kicad/README.md) — project entry points and local dependencies.

Revision copies, verification reports, renders, snapshots and archives are not kept beside the sources. Normal Git history is the only recovery path for earlier states.
