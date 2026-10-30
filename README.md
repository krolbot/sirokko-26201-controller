# Sirokko 262.01 controller

> **DRAFT / NOT FOR ORDER / NO POWER / NO FLASH / NO FIRE.** The board is unfinished and has not passed manufacturing or physical acceptance. Do not order it, power it, flash it, or connect it to a burner.

The working tree contains one current, complete KiCad project:

- project: [`hardware/kicad/Sirokko26201_control_motor_candidate.kicad_pro`](hardware/kicad/Sirokko26201_control_motor_candidate.kicad_pro);
- schematic: [`hardware/kicad/Sirokko26201_control_motor_candidate.kicad_sch`](hardware/kicad/Sirokko26201_control_motor_candidate.kicad_sch);
- PCB: [`hardware/kicad/Sirokko26201_control_motor_candidate.kicad_pcb`](hardware/kicad/Sirokko26201_control_motor_candidate.kicad_pcb);
- current PCB SHA-256: `351c76ea07d4622820c5444107610fd71b36d6842d54ab2a8f3dfad61c8d3b6f`.

The current PCB contains 144 footprints, 1,536 segments, 447 vias and 35 zones. Relative to the immediately preceding published WIP, exactly one isolated F.Cu `/+3V3` segment was removed: UUID `248bebb3-e137-4f67-b1e3-b978ff56dd51`, `(286.1375,24.05)→(287.2,24.05)`, width 0.25 mm. No other copper changed, and the complete 5,684-pair PAD connectivity set is identical. The six verified F.Cu `/GND` segments remain: they join R86.2 and R87.2 to U11/C21/C22 and join the 24-pad J1/J3 component to the checked 70-pad main GND component, producing one 94-pad component. This is bounded WIP verification, not whole-board acceptance. Routing remains incomplete. The separate RESET button, J24, R702 and `RESET_RELEASED` net were removed without a replacement header, jumper or hidden button. U6.4 is tied to `+3V3`; the watchdog, hardware latch, automatic reset and requirement for a new `ARM_PULSE` remain.

The latest native KiCad 10.0.7 check reported 60 DRC warnings, 20 unconnected items and no schematic-parity issues. ERC reports one `isolated_pin_label` warning for `EN_CHIP_PU`. This is unfinished board state, not manufacturing acceptance.

The revoked bare-TFT M2×12 fastener model has been removed. The official nominal Waveshare 18366 STEP remains, but the factory standoff thread and height, mating hardware and full Z-stack are not accepted.

There is no firmware in the working tree. The former bench implementation was rejected as a basis for the final product and is available only through Git history. Future firmware must not restore the old manual-RESET functions; arming uses `ARM_PULSE` only during an allowed start. No ILI9341 driver is implemented.

Current documents:

- [`README_RU.md`](README_RU.md) — primary Russian entry point;
- [`docs/REQUIREMENTS_RU.md`](docs/REQUIREMENTS_RU.md) — requirements;
- [`docs/CURRENT_STATE_RU.md`](docs/CURRENT_STATE_RU.md) — current state and limitations;
- [`hardware/kicad/README.md`](hardware/kicad/README.md) — project entry points and local dependencies.

Revision copies, verification reports, renders, snapshots and archives are not kept beside the sources. Normal Git history is the only recovery path for earlier states.
