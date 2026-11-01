# Sirokko 262.01 controller

> **DRAFT / REVIEW REQUIRED / NOT FOR ORDER / NO POWER / NO FLASH / NO FIRE; firmware HOLD.** `production_release=false`. This repository is not a manufacturing release or evidence that the hardware works.

The repository contains one canonical KiCad project:

- project: [`hardware/kicad/Sirokko26201_control_motor_candidate.kicad_pro`](hardware/kicad/Sirokko26201_control_motor_candidate.kicad_pro);
- root schematic: [`hardware/kicad/Sirokko26201_control_motor_candidate.kicad_sch`](hardware/kicad/Sirokko26201_control_motor_candidate.kicad_sch);
- PCB: [`hardware/kicad/Sirokko26201_control_motor_candidate.kicad_pcb`](hardware/kicad/Sirokko26201_control_motor_candidate.kicad_pcb);
- PCB SHA-256: `9e10942d0e3e27358864b34ea380bc3fc3f7fcc24002788059a10cb5a8bf0444`;
- board outline: 305 × 170 mm.

All 70 footprints are now on F.Cu, the relay side. The removable ESP32 DevKit headers are J1=(100.45, 75.5) mm and J3=(100.45, 100.9) mm, both at −90°. The board contains 287 pads, 633 segments, 145 vias, 77 nets and 27 zones. Native KiCad 10.0.8 DRC reports 0 violations and 0 unconnected items; root ERC reports 0 violations. The separate full DRC with zone refill, all track errors and schematic parity returned exit code 5: geometry and connectivity lists are empty, `ignored_checks=[]`, but R108/C109 remain schematic-only WIP without PCB footprints. This is not a complete PCB/schematic parity PASS.

Current renders from this PCB:

- [front](docs/board-front.png);
- [back](docs/board-back.png), in board coordinates, not mirrored;
- [oblique 3D](docs/board-3d-oblique.png);
- [bottom 3D](docs/board-3d-bottom.png).

The applied front-side record is [`docs/research/ESP_FRONT_SIDE_CURRENT_RU.md`](docs/research/ESP_FRONT_SIDE_CURRENT_RU.md); machine details are in [`ESP_FRONT_SIDE_CURRENT.json`](docs/research/ESP_FRONT_SIDE_CURRENT.json), and the current full report is [`FRONT_SIDE_FULL_DRC.json`](docs/research/FRONT_SIDE_FULL_DRC.json). The single current manufacturing-data directory is [`manufacturing/current/`](manufacturing/current/): Gerber, separate PTH/NPTH Excellon, BOM, full top-side placement CSV and review PDFs. It is **DRAFT / NOT FOR ORDER**, not a release; an independent CAM-viewer review of the copper files was not performed.

Requirements remain in [`docs/REQUIREMENTS_RU.md`](docs/REQUIREMENTS_RU.md); current limitations are summarized in [`docs/CURRENT_STATE_RU.md`](docs/CURRENT_STATE_RU.md). Startup, SOA and input-transient coordination remain **NOT QUALIFIED**; firmware remains on HOLD; physical tests were not performed. Bodies of ESP32/MAX31856 modules are not included in 3D, only their headers. Old revision projects, duplicate PCB snapshots and PCB ZIP copies are not kept in the working tree; Git history remains the recovery path.
