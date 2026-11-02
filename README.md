# Sirokko 262.01 controller

> **DRAFT / REVIEW REQUIRED / NOT FOR ORDER / NO POWER / NO FLASH / NO FIRE; firmware HOLD.** `production_release=false`. This repository is not a manufacturing release or evidence that the hardware works.

The repository contains one canonical KiCad project:

- project: [`hardware/kicad/Sirokko26201_control_motor_candidate.kicad_pro`](hardware/kicad/Sirokko26201_control_motor_candidate.kicad_pro);
- root schematic: [`hardware/kicad/Sirokko26201_control_motor_candidate.kicad_sch`](hardware/kicad/Sirokko26201_control_motor_candidate.kicad_sch);
- PCB: [`hardware/kicad/Sirokko26201_control_motor_candidate.kicad_pcb`](hardware/kicad/Sirokko26201_control_motor_candidate.kicad_pcb);
- PCB SHA-256: `1b7a0936d9b96aaec3050cd86b12ce0b6929f403520aee953a22be1cb03e8a96`;
- board outline: 305 × 170 mm.

All 70 footprints are on F.Cu, the relay side. The removable ESP32 DevKit headers are J1=(100.45, 75.5) mm and J3=(100.45, 100.9) mm, both at −90°. The board contains 287 pads, 633 segments, 145 vias, 77 nets and 27 zones. A bounded native edit enlarged 75 safe 0.60/0.30 mm via lands to 0.80/0.30 mm; 28 conflicting small vias remain unchanged. UUIDs, drills, positions, nets, layers and all track geometry were preserved. Native KiCad 10.0.8 DRC after refill reports 0 violations and 0 unconnected items. The separate schematic-parity run returned exit code 5 only for the existing schematic-only R108/C109 footprints. See the [native via acceptance record](docs/research/VIA_RING_NATIVE_ACCEPTANCE_CURRENT_RU.md).

Current renders from this PCB:

- [front](docs/board-front.png);
- [back](docs/board-back.png), mirrored as viewed from below;
- [oblique 3D](docs/board-3d-oblique.png);
- [bottom 3D](docs/board-3d-bottom.png).

The applied front-side record is [`docs/research/ESP_FRONT_SIDE_CURRENT_RU.md`](docs/research/ESP_FRONT_SIDE_CURRENT_RU.md); machine details are in [`ESP_FRONT_SIDE_CURRENT.json`](docs/research/ESP_FRONT_SIDE_CURRENT.json), and the current full report is [`FRONT_SIDE_FULL_DRC.json`](docs/research/FRONT_SIDE_FULL_DRC.json). The current control-pad map is [`CONTROL_CONNECTIONS_CURRENT_RU.md`](docs/research/CONTROL_CONNECTIONS_CURRENT_RU.md) with [CSV data](docs/research/CONTROL_CONNECTIONS_CURRENT.csv); it is not a final GPIO contract, and U11 `~FAULT` is not connected to the ESP.

The existing manufacturing-data directory [`manufacturing/current/`](manufacturing/current/) was generated from the previous PCB SHA `9e10942d…` and is now explicitly stale. Its Gerber, Excellon, BOM, placement data, PDFs and manifest must not be used for an order or represented as current. The manufacturing set will be regenerated only after the remaining PCB/schematic parity work is closed. Current hobby-fabrication guidance is in [FABRICATION_REQUIREMENTS_CURRENT_RU.md](docs/research/FABRICATION_REQUIREMENTS_CURRENT_RU.md).

The EVM-derived ngspice fixture now produces about 12.22 V after correcting its own RON connection; the library and PCB were not changed. This is documented in the [follow-up](docs/research/INPUT_STARTUP_FOLLOWUP_RU.md), [machine result](docs/research/SPICE_COMPATIBILITY_CURRENT.json) and [plot](docs/research/SPICE_RON_CURRENT.png). The VSWITCH/dispatcher incompatibility is no longer a current blocker: the installed calculation path passes the control cases, while the stock-library defect remains reproduced only in an isolated negative control. A new [imposed-ramp diagnostic](docs/research/INPUT_SLEW_BUCK_CURRENT_RU.md) ([JSON](docs/research/INPUT_SLEW_BUCK_CURRENT.json), [compact CSV](docs/research/INPUT_SLEW_BUCK_TRACE_CURRENT.csv), [plot](docs/research/INPUT_SLEW_BUCK_CURRENT.png)) reaches 4.75 V at 9.32478073 ms during a 0→28.8 V input ramp and stays above that threshold through 16 ms. The follow-up [input-storage and feed-current calculation](docs/research/INPUT_STORAGE_CURRENT_RU.md) ([JSON](docs/research/INPUT_STORAGE_CURRENT.json), [plot](docs/research/INPUT_STORAGE_CURRENT.png), [reproducible inputs and compact results](docs/research/input_storage_current/)) uses the PCB's nominal 434.5 µF bus capacitance and assigned 10/100 mΩ feed-resistance sensitivity cases. It gives 2.09947/1.56676 A feed-path peaks and 5.03665/5.03651 V final-millisecond means; the roughly 108 A local buck-branch spikes are not feed-path current. Its single-FET 31.5722/30.9316 W and 0.183001/0.181634 J values are conditional scale checks, not SOA acceptance. These are diagnostic cases, not a model of the LM74800 gate drive, actual input MOSFET SOA, capacitor DC bias/ESL, tolerances, repeated starts or the complete board. The separate [24→5 V diagnostic](docs/research/BUCK_STARTUP_CURRENT_RU.md) still falls to about 0.977 V during restart after its forced input event; uninterrupted ESP power is not established. Reproducible older inputs are in [`docs/research/spice/`](docs/research/spice/); the TI model itself is not redistributed.

The current [ChipDip snapshot](docs/Sirokko_components_ChipDip.html) and [CSV](docs/Sirokko_components_ChipDip.csv) cover 38 board rows, 5 already agreed external items and 2 schematic-only WIP rows that must not be ordered. Prices and stock are a dated website snapshot, not a guarantee.

Requirements remain in [`docs/REQUIREMENTS_RU.md`](docs/REQUIREMENTS_RU.md); current limitations are summarized in [`docs/CURRENT_STATE_RU.md`](docs/CURRENT_STATE_RU.md). Full startup, model accuracy, SOA and input-transient coordination remain **NOT QUALIFIED**; firmware is not present in this tree and remains on HOLD; physical tests were not performed. Bodies of ESP32/MAX31856 modules are not included in 3D, only their headers. Old revision projects, duplicate PCB snapshots and PCB ZIP copies are not kept in the working tree; Git history remains the recovery path.
