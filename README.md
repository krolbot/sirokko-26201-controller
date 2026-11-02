# Sirokko 262.01 controller

> **DRAFT / REVIEW REQUIRED / NOT FOR ORDER / NO POWER / NO FLASH / NO FIRE; firmware HOLD.** `production_release=false`. This repository is not a manufacturing release or evidence that the hardware works.

The repository contains one canonical KiCad project:

- project: [`hardware/kicad/Sirokko26201_control_motor_candidate.kicad_pro`](hardware/kicad/Sirokko26201_control_motor_candidate.kicad_pro);
- root schematic: [`hardware/kicad/Sirokko26201_control_motor_candidate.kicad_sch`](hardware/kicad/Sirokko26201_control_motor_candidate.kicad_sch), SHA-256 `c8c5bd6e2bc0cb61428037c93b54b6c5d26ea97d9e6529f3facb100935ee3b3b`;
- PCB: [`hardware/kicad/Sirokko26201_control_motor_candidate.kicad_pcb`](hardware/kicad/Sirokko26201_control_motor_candidate.kicad_pcb), SHA-256 `46b84ef09d00a724962753ac0e83492b63a43177d69b33a519c890d5be9a0820`;
- board outline: 305 × 170 mm.

All 72 footprints are on F.Cu. The board contains 291 pads, 639 segments, 147 vias, 78 nets and 27 zones. R108/C109 are integrated next to U13; C24 is Samsung `CL21B224KCFSFNE` with its footprint and nets unchanged. All 70 prior footprints, 633 segments, 145 vias and existing pads were preserved; only R108/C109, six segments and two vias were added.

KiCad 10.0.8 checks: root ERC 0; native DRC 0 violations/0 unconnected items; Pre-fab Gate passed with schematic parity enabled and `ignored_checks=[]`. The [current verification report](docs/research/PCB_SCH_PARITY_CURRENT_RU.md) and [machine summary](docs/research/RC_INTEGRATION_CURRENT.json) do not authorize manufacturing or power-up.

Current renders and connection data:

- [front](docs/board-front.png);
- [back](docs/board-back.png), mirrored as viewed from below;
- [top 3D](docs/board-3d-oblique.png);
- [bottom 3D](docs/board-3d-bottom.png);
- [external connection map](docs/research/CONTROL_CONNECTIONS_CURRENT_RU.md) and [pinout image](docs/research/CONTROL_CONNECTIONS_CURRENT.png).

The current draft CAM/BOM package is in [`manufacturing/current/`](manufacturing/current/) and remains **NOT FOR ORDER**. Current hobby-fabrication guidance is in [FABRICATION_REQUIREMENTS_CURRENT_RU.md](docs/research/FABRICATION_REQUIREMENTS_CURRENT_RU.md).

The EVM-derived ngspice fixture now produces about 12.22 V after correcting its own RON connection; the library and PCB were not changed. This is documented in the [follow-up](docs/research/INPUT_STARTUP_FOLLOWUP_RU.md), [machine result](docs/research/SPICE_COMPATIBILITY_CURRENT.json) and [plot](docs/research/SPICE_RON_CURRENT.png). The VSWITCH/dispatcher incompatibility is no longer a current blocker: the installed calculation path passes the control cases, while the stock-library defect remains reproduced only in an isolated negative control. A new [imposed-ramp diagnostic](docs/research/INPUT_SLEW_BUCK_CURRENT_RU.md) ([JSON](docs/research/INPUT_SLEW_BUCK_CURRENT.json), [compact CSV](docs/research/INPUT_SLEW_BUCK_TRACE_CURRENT.csv), [plot](docs/research/INPUT_SLEW_BUCK_CURRENT.png)) reaches 4.75 V at 9.32478073 ms during a 0→28.8 V input ramp and stays above that threshold through 16 ms. The follow-up [input-storage and feed-current calculation](docs/research/INPUT_STORAGE_CURRENT_RU.md) ([JSON](docs/research/INPUT_STORAGE_CURRENT.json), [plot](docs/research/INPUT_STORAGE_CURRENT.png), [reproducible inputs and compact results](docs/research/input_storage_current/)) uses the PCB's nominal 434.5 µF bus capacitance and assigned 10/100 mΩ feed-resistance sensitivity cases. It gives 2.09947/1.56676 A feed-path peaks and 5.03665/5.03651 V final-millisecond means; the roughly 108 A local buck-branch spikes are not feed-path current. Its single-FET 31.5722/30.9316 W and 0.183001/0.181634 J values are conditional scale checks, not SOA acceptance. These are diagnostic cases, not a model of the LM74800 gate drive, actual input MOSFET SOA, capacitor DC bias/ESL, tolerances, repeated starts or the complete board. The separate [24→5 V diagnostic](docs/research/BUCK_STARTUP_CURRENT_RU.md) still falls to about 0.977 V during restart after its forced input event; uninterrupted ESP power is not established. Reproducible older inputs are in [`docs/research/spice/`](docs/research/spice/); the TI model itself is not redistributed.

The current [ChipDip snapshot](docs/Sirokko_components_ChipDip.html) and [CSV](docs/Sirokko_components_ChipDip.csv) cover 40 board/insertable rows and 5 external items. C24, R108 and C109 are reflected in CAD but remain not authorized for ordering. Prices and stock are a dated website snapshot, not a guarantee.

Requirements remain in [`docs/REQUIREMENTS_RU.md`](docs/REQUIREMENTS_RU.md); current limitations are summarized in [`docs/CURRENT_STATE_RU.md`](docs/CURRENT_STATE_RU.md). Full startup, model accuracy, SOA and input-transient coordination remain **NOT QUALIFIED**; firmware is not present in this tree and remains on HOLD; physical tests were not performed. Bodies of ESP32/MAX31856 modules are not included in 3D, only their headers. Old revision projects, duplicate PCB snapshots and PCB ZIP copies are not kept in the working tree; Git history remains the recovery path.
