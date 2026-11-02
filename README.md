# Sirokko 262.01 controller

> **DRAFT / REVIEW REQUIRED / NOT FOR ORDER / NO POWER / NO PHYSICAL FLASH / NO FIRE.** `production_release=false`, `physical_tests=false`. The existing firmware DRAFT is retained for review, but further firmware development, test expansion and target builds are on HOLD until Nikita agrees the functions, menu and overall logic. This repository is not a manufacturing release or evidence that the hardware works.

The repository contains one canonical KiCad project:

- project: [`hardware/kicad/Sirokko26201_control_motor_candidate.kicad_pro`](hardware/kicad/Sirokko26201_control_motor_candidate.kicad_pro);
- root schematic: [`hardware/kicad/Sirokko26201_control_motor_candidate.kicad_sch`](hardware/kicad/Sirokko26201_control_motor_candidate.kicad_sch), SHA-256 `0f62b97606d5220ebe1cb630b59fd2050777e6d9020e623b93a4c5518774f76e`;
- PCB: [`hardware/kicad/Sirokko26201_control_motor_candidate.kicad_pcb`](hardware/kicad/Sirokko26201_control_motor_candidate.kicad_pcb), SHA-256 `57f4b9437de4176f61e99d71ef923711f1368ba13d1de3b72c6fd13d82a4d09b`;
- board outline: 305 × 170 mm.

All 72 footprints are on F.Cu. The board contains 291 pads, 639 segments, 146 vias, 78 nets and 27 zones. One `/TC_SDI_MOSI` via was removed without increasing net length; additional candidates were rejected by DRC/connectivity checks. R108/C109 and Samsung `CL21B224KCFSFNE` remain unchanged.

KiCad 10.0.8 checks: root ERC 0; native DRC 0 violations/0 unconnected items; Pre-fab Gate passed with schematic parity enabled and `ignored_checks=[]`. The [current verification report](docs/research/PCB_SCH_PARITY_CURRENT_RU.md) and [machine summary](docs/research/RC_INTEGRATION_CURRENT.json) do not authorize manufacturing or power-up.

F1 is Littelfuse `0997015.WXN`, 15 A / 58 V DC, in the unchanged Keystone 3568 holder. The assigned interface envelope is: glow 7 A continuous / 10 A for at most 1 s; clutch 2 A / 3 A for at most 1 s; common J13/F1 input 10 A / 13 A for at most 1 s; ambient at F1 at most 80 °C. The provisional 5 V continuous budget is 0.70 A. See the [engineering basis](docs/research/SIROKKO_POWER_AND_LOAD_ENVELOPE_RU.md); physical qualification remains required.

Current renders and connection data:

- [front assembly](manufacturing/current/renders/assembly-front.png);
- [back assembly](manufacturing/current/renders/assembly-back.png);
- [top 3D](manufacturing/current/renders/board-3d-top.png);
- [bottom 3D](manufacturing/current/renders/board-3d-bottom.png);
- [F.Cu routing](manufacturing/current/renders/routing-F_Cu.png) and [B.Cu routing](manufacturing/current/renders/routing-B_Cu.png), with the bottom layer shown unmirrored;
- [external connection map](docs/research/CONTROL_CONNECTIONS_CURRENT_RU.md) and [pinout image](docs/research/CONTROL_CONNECTIONS_CURRENT.png).

The current draft CAM/BOM package is in [`manufacturing/current/`](manufacturing/current/) and remains **NOT FOR ORDER**. Current hobby-fabrication guidance is in [FABRICATION_REQUIREMENTS_CURRENT_RU.md](docs/research/FABRICATION_REQUIREMENTS_CURRENT_RU.md).

The EVM-derived ngspice fixture now produces about 12.22 V after correcting its own RON connection; the library and PCB were not changed. This is documented in the [follow-up](docs/research/INPUT_STARTUP_FOLLOWUP_RU.md), [machine result](docs/research/SPICE_COMPATIBILITY_CURRENT.json) and [plot](docs/research/SPICE_RON_CURRENT.png). The VSWITCH/dispatcher incompatibility is no longer a current blocker: the installed calculation path passes the control cases, while the stock-library defect remains reproduced only in an isolated negative control. A new [imposed-ramp diagnostic](docs/research/INPUT_SLEW_BUCK_CURRENT_RU.md) ([JSON](docs/research/INPUT_SLEW_BUCK_CURRENT.json), [compact CSV](docs/research/INPUT_SLEW_BUCK_TRACE_CURRENT.csv), [plot](docs/research/INPUT_SLEW_BUCK_CURRENT.png)) reaches 4.75 V at 9.32478073 ms during a 0→28.8 V input ramp and stays above that threshold through 16 ms. The follow-up [input-storage and feed-current calculation](docs/research/INPUT_STORAGE_CURRENT_RU.md) ([JSON](docs/research/INPUT_STORAGE_CURRENT.json), [plot](docs/research/INPUT_STORAGE_CURRENT.png), [reproducible inputs and compact results](docs/research/input_storage_current/)) uses the PCB's nominal 434.5 µF bus capacitance and assigned 10/100 mΩ feed-resistance sensitivity cases. It gives 2.09947/1.56676 A feed-path peaks and 5.03665/5.03651 V final-millisecond means; the roughly 108 A local buck-branch spikes are not feed-path current. Its single-FET 31.5722/30.9316 W and 0.183001/0.181634 J values are conditional scale checks, not SOA acceptance. These are diagnostic cases, not a model of the LM74800 gate drive, actual input MOSFET SOA, capacitor DC bias/ESL, tolerances, repeated starts or the complete board. The separate [24→5 V diagnostic](docs/research/BUCK_STARTUP_CURRENT_RU.md) still falls to about 0.977 V during restart after its forced input event; uninterrupted ESP power is not established. Reproducible older inputs are in [`docs/research/spice/`](docs/research/spice/); the TI model itself is not redistributed.

The current [ChipDip snapshot](docs/Sirokko_components_ChipDip.html) and [CSV](docs/Sirokko_components_ChipDip.csv) cover 40 board/insertable rows and 5 external items. C24, R108 and C109 are reflected in CAD but remain not authorized for ordering. Prices and stock are a dated website snapshot, not a guarantee.

A separate local panel is now planned: a display and one pressable encoder, with no separate buttons and a connection no longer than 10 cm. This does not restore the removed display or controls on the main PCB, and the AP/browser interface remains. The agreed gestures, unresolved hardware choices and firmware HOLD are recorded in [`docs/PANEL_CONTROL_RU.md`](docs/PANEL_CONTROL_RU.md).

Requirements remain in [`docs/REQUIREMENTS_RU.md`](docs/REQUIREMENTS_RU.md); current limitations are summarized in [`docs/CURRENT_STATE_RU.md`](docs/CURRENT_STATE_RU.md). The current source set combines this hardware project with the software DRAFT in [`firmware/`](firmware/); build, AP and binary-package instructions are in [`firmware/README_RU.md`](firmware/README_RU.md). The fire profile is deliberately unqualified and fuel remains software-blocked. Full startup, model accuracy, SOA, input-transient coordination and all physical firmware/radio/GPIO/load tests remain **NOT QUALIFIED**. Bodies of ESP32/MAX31856 modules are not included in 3D, only their headers. Old revision projects, duplicate PCB snapshots and PCB ZIP copies are not kept in the working tree; Git history remains the recovery path.
