# Sirokko 262.01 controller

> **DRAFT / REVIEW REQUIRED / NOT FOR ORDER / NO POWER / NO FLASH / NO FIRE; firmware HOLD.** This repository is not a manufacturing release or evidence that the hardware works.

The repository contains one canonical KiCad project:

- project: [`hardware/kicad/Sirokko26201_control_motor_candidate.kicad_pro`](hardware/kicad/Sirokko26201_control_motor_candidate.kicad_pro);
- root schematic: [`hardware/kicad/Sirokko26201_control_motor_candidate.kicad_sch`](hardware/kicad/Sirokko26201_control_motor_candidate.kicad_sch);
- PCB: [`hardware/kicad/Sirokko26201_control_motor_candidate.kicad_pcb`](hardware/kicad/Sirokko26201_control_motor_candidate.kicad_pcb);
- PCB SHA-256: `ede60af2ec3cc225c12836b2bf58f1e7a909507fb97195a61841afc22681ad20`;
- board outline: 305 × 170 mm.

The removable ESP32 DevKit headers are now at J1=(100.45, 100.9) mm and J3=(100.45, 75.5) mm, both at 90° on B.Cu. The current board contains 70 footprints, 287 pads, 615 segments, 140 vias and 27 zones. KiCad 10.0.8 native DRC reports 0 violations and 0 unconnected items; root ERC reports 0 violations. A separate full DRC with zone refill, all track errors and schematic parity returned exit code 5 only for the two known missing PCB footprints R108/C109; `ignored_checks` is empty and the PCB hash remained unchanged. Native connectivity readback preserved all 287 pad identities/nets and all 1600 physical pad pairs. These are CAD checks only.

Current renders from this PCB:

- [front](docs/board-front.png);
- [back](docs/board-back.png), mirrored for viewing from the back side;
- [oblique 3D](docs/board-3d-oblique.png);
- [bottom 3D](docs/board-3d-bottom.png).

The bounded acceptance report is [`docs/research/ESP_RELOCATION_ACCEPTANCE_RU.md`](docs/research/ESP_RELOCATION_ACCEPTANCE_RU.md); the current full DRC report is [`docs/research/ESP_RELOCATION_FULL_DRC.json`](docs/research/ESP_RELOCATION_FULL_DRC.json). Requirements remain in [`docs/REQUIREMENTS_RU.md`](docs/REQUIREMENTS_RU.md); current limitations are summarized in [`docs/CURRENT_STATE_RU.md`](docs/CURRENT_STATE_RU.md). No current manufacturing package or firmware release is included. Old revision projects, candidate copies, generated CAM archives and stale board exports are not kept in the working tree; Git history remains the recovery path.
