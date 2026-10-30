# Sirokko 262.01 controller

> **DRAFT / NOT FOR ORDER / NO POWER / NO FLASH / NO FIRE.** The board is unfinished and has not passed manufacturing or physical acceptance. Do not order it, power it, flash it, or connect it to a burner.

The working tree contains one current, complete KiCad project:

- project: [`hardware/kicad/Sirokko26201_control_motor_candidate.kicad_pro`](hardware/kicad/Sirokko26201_control_motor_candidate.kicad_pro);
- schematic: [`hardware/kicad/Sirokko26201_control_motor_candidate.kicad_sch`](hardware/kicad/Sirokko26201_control_motor_candidate.kicad_sch);
- PCB: [`hardware/kicad/Sirokko26201_control_motor_candidate.kicad_pcb`](hardware/kicad/Sirokko26201_control_motor_candidate.kicad_pcb);
- current PCB SHA-256: `aae5b6afbbb5283542778262591795df049b703988a5036539b7ebb61fa9f47a`.

The current PCB contains 144 footprints, 1,522 segments, 442 vias and 35 zones. The revoked external `EN_CHIP_PU` path was removed address-by-address: exactly 14 target 0.25 mm segments and five target 0.6/0.3 mm vias were deleted, while J1.3 kept its pad UUID, geometry and position and now belongs to `unconnected-(J1-Pin_3-Pad3)`. The root schematic contains one NC flag on J1.3 and no `EN_CHIP_PU` label or stub wire. All other copper and pad assignments are unchanged, and the complete 5,684-pair PAD connectivity set remains identical. The DevKit's internal EN button and USB auto-reset were not changed. The separate RESET button, J24, R702 and `RESET_RELEASED` net remain removed; U6.4 stays tied to `+3V3`, and the watchdog, hardware latch, automatic reset and requirement for a new `ARM_PULSE` remain.

The latest native KiCad 10.0.7 check reported 58 DRC warnings, 19 unconnected items, no schematic-parity issues and zero ERC violations. This is unfinished board state, not manufacturing acceptance.

The revoked bare-TFT M2×12 fastener model has been removed. The official nominal Waveshare 18366 STEP remains, but the factory standoff thread and height, mating hardware and full Z-stack are not accepted.

There is no firmware in the working tree. The former bench implementation was rejected as a basis for the final product and is available only through Git history. Future firmware must not restore the old manual-RESET functions; arming uses `ARM_PULSE` only during an allowed start. No ILI9341 driver is implemented.

Current documents:

- [`README_RU.md`](README_RU.md) — primary Russian entry point;
- [`docs/REQUIREMENTS_RU.md`](docs/REQUIREMENTS_RU.md) — requirements;
- [`docs/CURRENT_STATE_RU.md`](docs/CURRENT_STATE_RU.md) — current state and limitations;
- [`hardware/kicad/README.md`](hardware/kicad/README.md) — project entry points and local dependencies.

Revision copies, verification reports, renders, snapshots and archives are not kept beside the sources. Normal Git history is the only recovery path for earlier states.
