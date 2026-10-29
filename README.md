# Sirokko 262.01 controller

> **DRAFT / NOT FOR ORDER / NO POWER / NO FIRE.** The board is unfinished and has not passed manufacturing or physical acceptance. Do not order it, power it, or connect it to a burner.

The working tree contains one current, complete KiCad project:

- project: [`hardware/kicad/Sirokko26201_control_motor_candidate.kicad_pro`](hardware/kicad/Sirokko26201_control_motor_candidate.kicad_pro);
- schematic: [`hardware/kicad/Sirokko26201_control_motor_candidate.kicad_sch`](hardware/kicad/Sirokko26201_control_motor_candidate.kicad_sch);
- PCB: [`hardware/kicad/Sirokko26201_control_motor_candidate.kicad_pcb`](hardware/kicad/Sirokko26201_control_motor_candidate.kicad_pcb);
- current PCB SHA-256: `f0cbceafcff1f6845bc850b090935e0a5f719f1fe8f087e952177cfb92e8722f`.

The current PCB contains 158 footprints, 603 physical pads, 1,384 segments and 401 vias. The latest native check reported 83 DRC violations and 80 unconnected items; ERC reported no violations. This is unfinished board state, not manufacturing acceptance.

The revoked bare-TFT M2×12 fastener model has been removed. The official nominal Waveshare 18366 STEP remains, but the factory standoff thread and height, mating hardware and full Z-stack are not accepted.

There is no firmware in the working tree. The former bench implementation was rejected as a basis for the final product and is available only through Git history. New firmware must wait for final PCB and pin-map acceptance; no ILI9341 driver is implemented.

Current documents:

- [`README_RU.md`](README_RU.md) — primary Russian entry point;
- [`docs/REQUIREMENTS_RU.md`](docs/REQUIREMENTS_RU.md) — requirements;
- [`docs/CURRENT_STATE_RU.md`](docs/CURRENT_STATE_RU.md) — current state and limitations;
- [`hardware/kicad/README.md`](hardware/kicad/README.md) — project entry points and local dependencies.

Revision copies, verification reports, renders, snapshots and archives are not kept beside the sources. Normal Git history is the only recovery path for earlier states.
