# Sirokko 262.01 controller

> **DRAFT / NOT FOR ORDER / NO POWER / NO FIRE.** The board is unfinished and has not passed manufacturing or physical acceptance. Do not order it, power it, or connect it to a burner.

The working tree contains one current, complete KiCad project:

- project: [`hardware/kicad/Sirokko26201_control_motor_candidate.kicad_pro`](hardware/kicad/Sirokko26201_control_motor_candidate.kicad_pro);
- schematic: [`hardware/kicad/Sirokko26201_control_motor_candidate.kicad_sch`](hardware/kicad/Sirokko26201_control_motor_candidate.kicad_sch);
- PCB: [`hardware/kicad/Sirokko26201_control_motor_candidate.kicad_pcb`](hardware/kicad/Sirokko26201_control_motor_candidate.kicad_pcb);
- current PCB SHA-256: `990424d8c90bf70402e3c629061e84942437e93cf4f28add89a7ec016eb3055e`.

The current PCB contains 144 footprints, 1,531 segments, 447 vias and 35 zones. Relative to the previous published WIP, four B.Cu `/+3V3` segments and no vias were added; no existing copper was removed or changed. Native all-PAD comparison changed the complete pair set from 3,724 to 3,988: no relations were lost and 264 `/+3V3` relations were added, so the former 24-pad and 11-pad components now form one 35-pad component. All GND PAD components are unchanged, including the checked 70-pad U303/C306/J23 component and the separate eight-pad C22.2 component. This is one verified WIP route, not whole-board rail acceptance. Routing remains incomplete. The separate RESET button, J24, R702 and `RESET_RELEASED` net were removed without a replacement header, jumper or hidden button. U6.4 is tied to `+3V3`; the watchdog, hardware latch, automatic reset and requirement for a new `ARM_PULSE` remain.

The latest native KiCad 10.0.7 check reported 61 DRC warnings, 23 unconnected items and no schematic-parity issues. ERC reports one `isolated_pin_label` warning for `EN_CHIP_PU`. This is unfinished board state, not manufacturing acceptance.

The revoked bare-TFT M2×12 fastener model has been removed. The official nominal Waveshare 18366 STEP remains, but the factory standoff thread and height, mating hardware and full Z-stack are not accepted.

There is no firmware in the working tree. The former bench implementation was rejected as a basis for the final product and is available only through Git history. Future firmware must not restore the old manual-RESET functions; arming uses `ARM_PULSE` only during an allowed start. No ILI9341 driver is implemented.

Current documents:

- [`README_RU.md`](README_RU.md) — primary Russian entry point;
- [`docs/REQUIREMENTS_RU.md`](docs/REQUIREMENTS_RU.md) — requirements;
- [`docs/CURRENT_STATE_RU.md`](docs/CURRENT_STATE_RU.md) — current state and limitations;
- [`hardware/kicad/README.md`](hardware/kicad/README.md) — project entry points and local dependencies.

Revision copies, verification reports, renders, snapshots and archives are not kept beside the sources. Normal Git history is the only recovery path for earlier states.
