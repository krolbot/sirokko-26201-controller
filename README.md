# Sirokko 262.01 controller

> **DRAFT / NOT FOR ORDER / NO POWER / NO FIRE.** The board is unfinished and has not passed manufacturing or physical acceptance. Do not order it, power it, or connect it to a burner.

The working tree contains one current, complete KiCad project:

- project: [`hardware/kicad/Sirokko26201_control_motor_candidate.kicad_pro`](hardware/kicad/Sirokko26201_control_motor_candidate.kicad_pro);
- schematic: [`hardware/kicad/Sirokko26201_control_motor_candidate.kicad_sch`](hardware/kicad/Sirokko26201_control_motor_candidate.kicad_sch);
- PCB: [`hardware/kicad/Sirokko26201_control_motor_candidate.kicad_pcb`](hardware/kicad/Sirokko26201_control_motor_candidate.kicad_pcb);
- current after30 PCB SHA-256: `1871ffa5460311b9283e2ff92f6249682ac68935023346bd98463a17cfca5180`.

Cleanup PCB `4a2f2871…` preserves all 1,721 C25/V8 copper objects and adds six segments; after20 adds 14 more. The current PCB preserves all 1,741 after20 objects by UUID, net, layer and geometry and adds exactly 10 more 0.25 mm F.Cu segments. See [`docs/CURRENT_STATE_RU.md`](docs/CURRENT_STATE_RU.md).

The revoked bare-TFT M2×12 fastener model has been removed. The official nominal Waveshare 18366 STEP remains, but the factory standoff thread and height, mating hardware and full Z-stack are not accepted.

There is no firmware in the working tree. The former bench implementation was rejected as a basis for the final product and remains available in Git history. New firmware must wait for final PCB and pin-map acceptance. No ILI9341 driver is currently implemented.

Current instructions and evidence:

- [`README_RU.md`](README_RU.md) — primary Russian entry point;
- [`docs/REQUIREMENTS_RU.md`](docs/REQUIREMENTS_RU.md) — requirements;
- [`docs/CURRENT_STATE_RU.md`](docs/CURRENT_STATE_RU.md) — current revision and limitations;
- [`hardware/kicad/README.md`](hardware/kicad/README.md) — project entry points and dependencies;
- [`verification/current/README_RU.md`](verification/current/README_RU.md) — fresh verification log.

Historical CAD copies, WIP candidates, superseded instructions and the old firmware were removed from the current tree; normal Git history remains the recovery path.
