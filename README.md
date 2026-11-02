# Sirokko 262.01 controller

> **DRAFT / REVIEW REQUIRED / NOT FOR ORDER / NO POWER / NO FLASH / NO FIRE; firmware HOLD.** `production_release=false`. This repository is not a manufacturing release or evidence that the hardware works.

The repository contains one canonical KiCad project:

- project: [`hardware/kicad/Sirokko26201_control_motor_candidate.kicad_pro`](hardware/kicad/Sirokko26201_control_motor_candidate.kicad_pro);
- root schematic: [`hardware/kicad/Sirokko26201_control_motor_candidate.kicad_sch`](hardware/kicad/Sirokko26201_control_motor_candidate.kicad_sch);
- PCB: [`hardware/kicad/Sirokko26201_control_motor_candidate.kicad_pcb`](hardware/kicad/Sirokko26201_control_motor_candidate.kicad_pcb);
- PCB SHA-256: `9e10942d0e3e27358864b34ea380bc3fc3f7fcc24002788059a10cb5a8bf0444`;
- board outline: 305 × 170 mm.

All 70 footprints are now on F.Cu, the relay side. The removable ESP32 DevKit headers are J1=(100.45, 75.5) mm and J3=(100.45, 100.9) mm, both at −90°. The board contains 287 pads, 633 segments, 145 vias, 77 nets and 27 zones. Native KiCad 10.0.8 DRC reports 0 violations and 0 unconnected items; root ERC reports 0 violations. The separate [full DRC and schematic-parity check](docs/research/PCB_SCH_PARITY_CURRENT_RU.md) returned exit code 5: geometry and connectivity lists are empty, `ignored_checks=[]`, but R108/C109 remain schematic-only WIP without PCB footprints. This is not a complete PCB/schematic parity PASS.

Current renders from this PCB:

- [front](docs/board-front.png);
- [back](docs/board-back.png), mirrored as viewed from below;
- [oblique 3D](docs/board-3d-oblique.png);
- [bottom 3D](docs/board-3d-bottom.png).

The applied front-side record is [`docs/research/ESP_FRONT_SIDE_CURRENT_RU.md`](docs/research/ESP_FRONT_SIDE_CURRENT_RU.md); machine details are in [`ESP_FRONT_SIDE_CURRENT.json`](docs/research/ESP_FRONT_SIDE_CURRENT.json), and the current full report is [`FRONT_SIDE_FULL_DRC.json`](docs/research/FRONT_SIDE_FULL_DRC.json). The current control-pad map is [`CONTROL_CONNECTIONS_CURRENT_RU.md`](docs/research/CONTROL_CONNECTIONS_CURRENT_RU.md) with [CSV data](docs/research/CONTROL_CONNECTIONS_CURRENT.csv); it is not a final GPIO contract, and U11 `~FAULT` is not connected to the ESP.

The single current manufacturing-data directory is [`manufacturing/current/`](manufacturing/current/): Gerber, separate PTH/NPTH Excellon, BOM, full top-side placement CSV, review PDFs and draft [fabrication requirements](manufacturing/current/FABRICATION_REQUIREMENTS_RU.md). A [GerbView import and gross visual review](docs/research/CAM_REVIEW_CURRENT_RU.md) was performed on all 9 Gerber and 2 Excellon files; it is not fine DFM or a manufacturing acceptance. The set remains **DRAFT / NOT FOR ORDER**, not a release.

The EVM-derived ngspice fixture now produces about 12.22 V after correcting its own RON connection; the library and PCB were not changed. This is documented in the [follow-up](docs/research/INPUT_STARTUP_FOLLOWUP_RU.md), [machine result](docs/research/SPICE_COMPATIBILITY_CURRENT.json) and [plot](docs/research/SPICE_RON_CURRENT.png). The VSWITCH/dispatcher incompatibility is no longer a current blocker: the installed calculation path passes the control cases, while the stock-library defect remains reproduced only in an isolated negative control. A new [imposed-ramp diagnostic](docs/research/INPUT_SLEW_BUCK_CURRENT_RU.md) ([JSON](docs/research/INPUT_SLEW_BUCK_CURRENT.json), [compact CSV](docs/research/INPUT_SLEW_BUCK_TRACE_CURRENT.csv), [plot](docs/research/INPUT_SLEW_BUCK_CURRENT.png)) reaches 4.75 V at 9.32478073 ms during a 0→28.8 V input ramp and stays above that threshold through 16 ms. The follow-up [input-storage and feed-current calculation](docs/research/INPUT_STORAGE_CURRENT_RU.md) ([JSON](docs/research/INPUT_STORAGE_CURRENT.json), [plot](docs/research/INPUT_STORAGE_CURRENT.png), [reproducible inputs and compact results](docs/research/input_storage_current/)) uses the PCB's nominal 434.5 µF bus capacitance and assigned 10/100 mΩ feed-resistance sensitivity cases. It gives 2.09947/1.56676 A feed-path peaks and 5.03665/5.03651 V final-millisecond means; the roughly 108 A local buck-branch spikes are not feed-path current. Its single-FET 31.5722/30.9316 W and 0.183001/0.181634 J values are conditional scale checks, not SOA acceptance. These are diagnostic cases, not a model of the LM74800 gate drive, actual input MOSFET SOA, capacitor DC bias/ESL, tolerances, repeated starts or the complete board. The separate [24→5 V diagnostic](docs/research/BUCK_STARTUP_CURRENT_RU.md) still falls to about 0.977 V during restart after its forced input event; uninterrupted ESP power is not established. Reproducible older inputs are in [`docs/research/spice/`](docs/research/spice/); the TI model itself is not redistributed.

The current [ChipDip snapshot](docs/Sirokko_components_ChipDip.html) and [CSV](docs/Sirokko_components_ChipDip.csv) cover 38 board rows, 5 already agreed external items and 2 schematic-only WIP rows that must not be ordered. Prices and stock are a dated website snapshot, not a guarantee.

Requirements remain in [`docs/REQUIREMENTS_RU.md`](docs/REQUIREMENTS_RU.md); current limitations are summarized in [`docs/CURRENT_STATE_RU.md`](docs/CURRENT_STATE_RU.md). Full startup, model accuracy, SOA and input-transient coordination remain **NOT QUALIFIED**; firmware is not present in this tree and remains on HOLD; physical tests were not performed. Bodies of ESP32/MAX31856 modules are not included in 3D, only their headers. Old revision projects, duplicate PCB snapshots and PCB ZIP copies are not kept in the working tree; Git history remains the recovery path.
