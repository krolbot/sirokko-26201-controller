# Sirokko 262.01 controller

> **DRAFT / NOT FOR ORDER / NO POWER / NO FLASH / NO FIRE.** The board is unfinished, has no manufacturing release, and has not passed physical acceptance. Do not order it, power it, flash it, or connect it to a burner.

The working tree contains one current KiCad project:

- project: [`hardware/kicad/Sirokko26201_control_motor_candidate.kicad_pro`](hardware/kicad/Sirokko26201_control_motor_candidate.kicad_pro);
- schematic: [`hardware/kicad/Sirokko26201_control_motor_candidate.kicad_sch`](hardware/kicad/Sirokko26201_control_motor_candidate.kicad_sch);
- PCB: [`hardware/kicad/Sirokko26201_control_motor_candidate.kicad_pcb`](hardware/kicad/Sirokko26201_control_motor_candidate.kicad_pcb);
- HC-160A-S2 interface: [`hardware/kicad/HC160_INTERFACE_RU.md`](hardware/kicad/HC160_INTERFACE_RU.md);
- current schematic SHA-256: `da4c369967fe13e25d25cd0f8f2cf3b1fae14dc50ef2acddf8c625d268300ecc`;
- unchanged PCB SHA-256: `b11c63de29efd64ce23d33bcd5e74f7ddecd919ec385467222ebf0c1102e6cf3`.

The schematic now uses the DFRobot DFR0650 SSD1306 128×64 OLED over I²C at the default address `0x3C` and 3.3 V. DS1 is provisionally represented as a 1×6 socket: `1=+3V3`, `2=GND`, `3=OLED_SCL`, `4=OLED_SDA`, `5/6=NC`; SDA is GPIO8/J1.12 and SCL is GPIO47/J3.17. C307 is 100 nF, MPN `GRM188R71E104KA01D`, footprint `C_0603_1608Metric`.

**Only the schematic has been updated. The PCB is deliberately unchanged and still contains the previous display circuitry, placement, copper and sheet metadata; it does not match the current schematic.** The earlier PCB DRC result belongs to the old SCH/PCB pairing and is not a validation of this mismatched WIP pair. The current schematic has a fresh KiCad 10.0.7 ERC result of zero violations, but that is not PCB acceptance or a manufacturing release.

The DFR0650 socket and mounting arrangement, mechanics, I²C routing and current budget are not accepted. Official references are kept as links only: [product page](https://wiki.dfrobot.com/Monochrome_0.96_128x64_IIC_SPI_OLED_Display_SKU_DFR0650), [module schematic](https://dfimg.dfrobot.com/wiki/21114/DFR0650_fermion-oled-display-module_schematics_V1.0.pdf), and [dimension drawing](https://dfimg.dfrobot.com/wiki/21114/DFR0650_fermion-oled-display-module_dimension_V1.0.pdf).

There is no approved firmware for this hardware contract. Firmware work remains blocked until the PCB is updated and accepted against the schematic and final pin map.

Current documents:

- [`README_RU.md`](README_RU.md) — primary Russian entry point;
- [`docs/REQUIREMENTS_RU.md`](docs/REQUIREMENTS_RU.md) — requirements;
- [`docs/CURRENT_STATE_RU.md`](docs/CURRENT_STATE_RU.md) — current state and limitations;
- [`hardware/kicad/README.md`](hardware/kicad/README.md) — project entry points and local dependencies.

Revision copies, verification reports, renders, snapshots and archives are not kept beside the sources. Normal Git history is the only recovery path for earlier states.
