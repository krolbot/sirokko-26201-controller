> **Stage status:** PCB design/layout and fabrication files are completed and accepted. Corrective firmware 1.0.0-dev3 integrates the approved landscape TFT and mobile web UI; host, browser and ESP32-S3 target checks pass. Prototype manufacture/assembly and physical tests remain separate stages. Missing physical tests do not block bare-prototype fabrication by themselves. [Подробнее / Stage definitions](docs/PROJECT_STAGES_RU.md).

# Sirokko 262.01 controller

Current [dev3 corrective acceptance](firmware/docs/ACCEPTANCE_DEV3_RU.md) covers the audited persistence/STOP/START, calibration, sensor, panel and OTA defects. The [dev3 prerelease](https://github.com/krolbot/sirokko-26201-controller/releases/tag/v1.0.0-dev3) preserves the verified package bytes; archive metadata describes its pre-publication state. Hardware qualification remains separate.

> **Hardware v7 candidate: verified digital package with 115 vias; optimization accepted by the user; further minimization is not required.** Bare PCB fabrication and component assembly are separate. See [current manufacturing instructions](manufacturing/current/README_RU.md). `production_release=false`, `physical_tests=false`: no physical qualification or operational release is claimed. Firmware development was authorized on 2026-11-06. The previous [1.0.0-dev1 prerelease](https://github.com/krolbot/sirokko-26201-controller/releases/tag/v1.0.0-dev1) contains the verified software package; [software acceptance](firmware/docs/ACCEPTANCE_RU.md) does not claim physical qualification.

The repository contains one canonical KiCad project:

- project: [`hardware/kicad/Sirokko26201_control_motor_candidate.kicad_pro`](hardware/kicad/Sirokko26201_control_motor_candidate.kicad_pro);
- root schematic: [`hardware/kicad/Sirokko26201_control_motor_candidate.kicad_sch`](hardware/kicad/Sirokko26201_control_motor_candidate.kicad_sch), SHA-256 `0b85bf2f4921b25a1c3126348eb176a88c7e3b29ca474c2d7229c4367e0a5cf5`;
- PCB: [`hardware/kicad/Sirokko26201_control_motor_candidate.kicad_pcb`](hardware/kicad/Sirokko26201_control_motor_candidate.kicad_pcb), SHA-256 `0ed3728e0cd427c01e9509a6aff25a2870087f775708396bfdd9fabb0b346e82`;
- board outline: 281 × 170 mm.

All 73 footprints are on F.Cu:303pads,663segments,115vias,25zones. Ready-made TFT2.4+EC11 connects through J32. [Exact pin map](docs/TFT24_INTERFACE_RU.md). No custom panel PCB.

KiCad10.0.8:ERC0 with four retained ignored checks; nativeDRC0/opens0 with ignored=[]. Independently compared252electricalpin-net assignments,251drill hits and60placement rows. [Current checks](manufacturing/current/checks/verification.json). Prior research reports naming other hashes are historical, not acceptance of this revision.

F1 is Littelfuse `0997015.WXN`, 15 A / 58 V DC, in the unchanged Keystone 3568 holder. The assigned interface envelope is: glow 7 A continuous / 10 A for at most 1 s; clutch 2 A / 3 A for at most 1 s; common J13/F1 input 10 A / 13 A for at most 1 s; ambient at F1 at most 80 °C. The provisional 5 V continuous budget is 0.70 A. See the [engineering basis](docs/research/SIROKKO_POWER_AND_LOAD_ENVELOPE_RU.md); physical qualification remains required.

Current renders and connection data:

- [front assembly](manufacturing/current/renders/assembly-front.png);
- [back copper](manufacturing/current/renders/routing-B_Cu.png);
- [top 3D](manufacturing/current/renders/board-3d-top.png);
- [bottom 3D](manufacturing/current/renders/board-3d-bottom.png);
- [Gerber/Excellon CAM](manufacturing/current/cam.zip);
- [external connection map](docs/research/CONTROL_CONNECTIONS_CURRENT_RU.md) and [pinout image](docs/research/CONTROL_CONNECTIONS_CURRENT.png).

The current CAM/BOM package is in [`manufacturing/current/`](manufacturing/current/). Its manifest and independent CAM review refer to the final KEY0 revision. Use its [README](manufacturing/current/README_RU.md) for fabrication; earlier research notes describe historical revisions.

The EVM-derived ngspice fixture now produces about 12.22 V after correcting its own RON connection; the library and PCB were not changed. This is documented in the [follow-up](docs/research/INPUT_STARTUP_FOLLOWUP_RU.md), [machine result](docs/research/SPICE_COMPATIBILITY_CURRENT.json) and [plot](docs/research/SPICE_RON_CURRENT.png). The VSWITCH/dispatcher incompatibility is no longer a current blocker: the installed calculation path passes the control cases, while the stock-library defect remains reproduced only in an isolated negative control. A new [imposed-ramp diagnostic](docs/research/INPUT_SLEW_BUCK_CURRENT_RU.md) ([JSON](docs/research/INPUT_SLEW_BUCK_CURRENT.json), [compact CSV](docs/research/INPUT_SLEW_BUCK_TRACE_CURRENT.csv), [plot](docs/research/INPUT_SLEW_BUCK_CURRENT.png)) reaches 4.75 V at 9.32478073 ms during a 0→28.8 V input ramp and stays above that threshold through 16 ms. The follow-up [input-storage and feed-current calculation](docs/research/INPUT_STORAGE_CURRENT_RU.md) ([JSON](docs/research/INPUT_STORAGE_CURRENT.json), [plot](docs/research/INPUT_STORAGE_CURRENT.png), [reproducible inputs and compact results](docs/research/input_storage_current/)) uses the PCB's nominal 434.5 µF bus capacitance and assigned 10/100 mΩ feed-resistance sensitivity cases. It gives 2.09947/1.56676 A feed-path peaks and 5.03665/5.03651 V final-millisecond means; the roughly 108 A local buck-branch spikes are not feed-path current. Its single-FET 31.5722/30.9316 W and 0.183001/0.181634 J values are conditional scale checks, not SOA acceptance. These are diagnostic cases, not a model of the LM74800 gate drive, actual input MOSFET SOA, capacitor DC bias/ESL, tolerances, repeated starts or the complete board. The separate [24→5 V diagnostic](docs/research/BUCK_STARTUP_CURRENT_RU.md) still falls to about 0.977 V during restart after its forced input event; uninterrupted ESP power is not established. Reproducible older inputs are in [`docs/research/spice/`](docs/research/spice/); the TI model itself is not redistributed.

The current [ChipDip snapshot](docs/Sirokko_components_ChipDip.html) and [CSV](docs/Sirokko_components_ChipDip.csv) cover 40 board/insertable rows and 5 external items. C24, R108 and C109 are reflected in CAD but remain not authorized for ordering. Prices and stock are a dated website snapshot, not a guarantee.

A ready-made TFT2.4+EC11+KEY0 panel connects through J32, cable up to100mm. KEY0 module12→J32.12→J3.8/GPIO40 is connected in hardware. START/STOP uses a short KEY0 press instead of encoder hold. TFT rendering, local menus and encoder navigation are implemented in firmware 1.0.0-dev3; they are host-tested and target-built, not tested on the physical panel. [Gestures](docs/PANEL_CONTROL_RU.md), [hardware pin map](docs/TFT24_INTERFACE_RU.md).

Requirements remain in [`docs/REQUIREMENTS_RU.md`](docs/REQUIREMENTS_RU.md); current limitations are summarized in [`docs/CURRENT_STATE_RU.md`](docs/CURRENT_STATE_RU.md). The current source set combines this hardware project with the development firmware 1.0.0-dev3 in [`firmware/`](firmware/); build, AP and binary-package instructions are in [`firmware/README_RU.md`](firmware/README_RU.md). The fire profile is deliberately unqualified and fuel remains software-blocked. Full startup, model accuracy, SOA, input-transient coordination and all physical firmware/radio/GPIO/load tests remain **NOT QUALIFIED**. Bodies of ESP32/MAX31856 modules are not included in 3D, only their headers. Old revision projects, duplicate PCB snapshots and PCB ZIP copies are not kept in the working tree; Git history remains the recovery path.
