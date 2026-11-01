# Sirokko 262.01 controller

> **DRAFT / REVIEW REQUIRED / NOT FOR ORDER / NO POWER / NO FLASH / NO FIRE; firmware HOLD.** This repository is not a manufacturing release or evidence that the hardware works.

The repository contains one canonical KiCad project:

- project: [`hardware/kicad/Sirokko26201_control_motor_candidate.kicad_pro`](hardware/kicad/Sirokko26201_control_motor_candidate.kicad_pro);
- root schematic: [`hardware/kicad/Sirokko26201_control_motor_candidate.kicad_sch`](hardware/kicad/Sirokko26201_control_motor_candidate.kicad_sch);
- PCB: [`hardware/kicad/Sirokko26201_control_motor_candidate.kicad_pcb`](hardware/kicad/Sirokko26201_control_motor_candidate.kicad_pcb);
- PCB SHA-256: `6b468c5fa23f7dce844ba41a267a18a37fbd6e21685c135afdaadc7fb7f77b5e`;
- board outline: 305 × 170 mm.

KiCad 10.0.8 refilled all 19 copper zones and reported DRC=0, ERC=0 and no unconnected PCB items. Native readback preserved 70 footprints, 287 pads, 617 segments, 139 vias, 27 zones and all 1600 physical pad pairs across the refill. `validate_board` reports no placement or board-edge violations. These are CAD checks only.

Current renders from the canonical PCB:

- [front](docs/board-front.png);
- [back](docs/board-back.png);
- [oblique 3D](docs/board-3d-oblique.png).

Requirements remain in [`docs/REQUIREMENTS_RU.md`](docs/REQUIREMENTS_RU.md); current limitations are summarized in [`docs/CURRENT_STATE_RU.md`](docs/CURRENT_STATE_RU.md). Old revision projects, candidate copies, generated CAM archives and stale board exports are not kept in the working tree. Git history remains the recovery path.
