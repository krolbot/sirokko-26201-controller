# Sirokko 262.01 controller

> **DRAFT / NOT FOR ORDER / NO POWER / NO FLASH / NO FIRE.** The board is unfinished, has no manufacturing release, and has not passed physical acceptance. Do not order it, power it, flash it, or connect it to a burner.

The working tree contains one current KiCad project:

- project: [`hardware/kicad/Sirokko26201_control_motor_candidate.kicad_pro`](hardware/kicad/Sirokko26201_control_motor_candidate.kicad_pro);
- schematic: [`hardware/kicad/Sirokko26201_control_motor_candidate.kicad_sch`](hardware/kicad/Sirokko26201_control_motor_candidate.kicad_sch);
- PCB: [`hardware/kicad/Sirokko26201_control_motor_candidate.kicad_pcb`](hardware/kicad/Sirokko26201_control_motor_candidate.kicad_pcb);
- HC-160A-S2 interface: [`hardware/kicad/HC160_INTERFACE_RU.md`](hardware/kicad/HC160_INTERFACE_RU.md);
- current schematic SHA-256: `fa32fe0e0873cced9abf44ff01d7157661011c2069e2f57414ffe11d8bf31b0e`;
- unchanged PCB SHA-256: `b11c63de29efd64ce23d33bcd5e74f7ddecd919ec385467222ebf0c1102e6cf3`.

The DFRobot DFR0650 selection is **revoked because its approximately RUB 3,230 price was rejected**. The current low-cost candidate is the 0.96-inch I²C 128×64 SSD1306 OLED sold as [ChipDip 9001709304](https://www.chipdip.ru/product/oled-0-96-i2c-belyy-9001709304) (RUB 530 per unit on the checked listing). DS1 is provisionally represented as a 1×4 socket, in the seller-photo order `GND/VCC/SCK/SDA`: `1=GND`, `2=+3V3`, `3=OLED_SCL`, `4=OLED_SDA`; SDA is GPIO8/J1.12 and SCL is GPIO47/J3.17. This reverses the first two pins relative to the revoked DFR0650 contract. C307 remains 100 nF, MPN `GRM188R71E104KA01D`, footprint `C_0603_1608Metric`.

**Only the schematic has been updated. The PCB is deliberately unchanged and still contains the previous display circuitry, placement, copper and sheet metadata; it does not match the current schematic.** The earlier PCB DRC result belongs to the old SCH/PCB pairing and is not a validation of this mismatched WIP pair. The current schematic has a fresh KiCad 10.0.7 ERC result of zero violations, but that is not PCB acceptance or a manufacturing release.

The seller card confirms 3.3–5 V, 128×64, I²C and SSD1306, but the exact manufacturer/revision, mounting drawing, module circuit and actual I²C address are unconfirmed. The generic `PinSocket_1x04_P2.54mm_Vertical` footprint is provisional and does not define the module outline or mounting holes; no mounting-hole positions have been assigned. DFR0650 or look-alike-board geometry and DFR0650 documentation must not be reused for this candidate. Socket/mounting mechanics, I²C routing and current budget are not accepted.

There is no approved firmware for this hardware contract. Firmware work remains blocked until the PCB is updated and accepted against the schematic and final pin map.

Current documents:

- [`README_RU.md`](README_RU.md) — primary Russian entry point;
- [`docs/REQUIREMENTS_RU.md`](docs/REQUIREMENTS_RU.md) — requirements;
- [`docs/CURRENT_STATE_RU.md`](docs/CURRENT_STATE_RU.md) — current state and limitations;
- [`hardware/kicad/README.md`](hardware/kicad/README.md) — project entry points and local dependencies.

Revision copies, verification reports, renders, snapshots and archives are not kept beside the sources. Normal Git history is the only recovery path for earlier states.
