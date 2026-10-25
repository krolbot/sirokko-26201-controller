# Sirokko 262.01 controller

> **DRAFT / BENCH ONLY / NOT FOR ORDER.** Main contains separate hardware WIP snapshots and `0.1.7-DRAFT` bench firmware. They are not one finished burner controller or a PCB manufacturing release. Russian documentation: [README_RU.md](README_RU.md).

## Current requirements and status

The [unified motor + SPI working directory](hardware/candidates/unified_motor_spi_working/README_RU.md) is the source for continued routing of the common PCB. Earlier canonical files and separate candidates remain unchanged historical snapshots, not parallel active sources. The current TFT +3V3/GND/EN supply routing has 251 segments / 72 vias, 213 unconnected items, 4 library warnings and 5 ignored DRC checks; the recorded DRC exit code is 5, not an overall PASS. The 194/52 motor + SPI baseline is preserved in commit [7ede3fca5f8371e4e3ec7252b4ba637994532fc2](https://github.com/krolbot/sirokko-26201-controller/commit/7ede3fca5f8371e4e3ec7252b4ba637994532fc2), with its historical reports and images retained separately. TFT supply connectivity is complete geometrically; the long EN route and 3.3 V budget are not qualified, and power routing and physical testing remain incomplete.

- [Requirements register](docs/REQUIREMENTS_RU.md) — all requirements, superseded decisions and acceptance criteria.
- [Current state and work plan](docs/CURRENT_STATE_RU.md) — published evidence, unfinished work and physical-test limits.
- [RTC and timer reference research](docs/RTC_TIMER_REFERENCE_RU.md) — pinned sources, battery-holder/RTC candidates and adaptation limits.
- [CR2032 holder selection](docs/CR_HOLDER_SELECTION_RU.md) — Keystone 1060 candidate, catalog/footprint geometry and polarity comparison, and remaining checks; not integrated, not FOR_ORDER.

These documents take precedence over historical requirements below. Four on-board buttons with combined START/STOP and a battery-backed RTC are new requirements, not verified features of the [0.1.7 bench kit](firmware/release/build-kits/0.1.7-esp32s3/). Earlier five-button, no-RTC and network-prohibition requirements are superseded; Wi-Fi/MQTT/OTA are required, without authorizing infrastructure deployment or calendar ignition. The current-state document distinguishes manifest evidence from the engineer's independent 0.1.7 host-test run. No firmware tests, target build or physical tests were rerun for this publication.

The sections below preserve the earlier recovered-hardware/0.1.5 checkpoint; use the current-state document for later snapshots.

## Historical hardware checkpoint

- `hardware/kicad/` — recovered main PCB, matching native KiCad 10.0.6 schematic, project, local libraries and separate draft sheets;
- `hardware/candidates/input-protection/` — a separate input-protection candidate, not integrated into the main PCB;
- [SPI recovered WIP candidate](hardware/candidates/spi_recovered/) — only `/TC_AIR_CS` is completed; 130 track segments, 35 vias, 181 unconnected items and the same 4 library warnings. It does not replace the canonical PCB; remaining SPI and power routing is unfinished. The candidate README includes both copper layers;
- `firmware/` — ESP-IDF 5.5.2 sources, host tests, I/O contract and versioned bench flashing sets;
- `docs/` — functional requirements, including the selected power-return behavior;
- `verification/wip-2026-10-25/` — recorded hardware checks. Earlier DRAFT-01/DRAFT-02 exports are historical, not evidence for the current PCB.

The recovered PCB has 88 footprints, 123 track segments, 32 vias and 3 copper zones. Recorded ERC reports no violations under the saved project settings; DRC still reports 4 library-footprint warnings and 182 unconnected items. The schematic-parity check reports 7 warnings. See [hardware/WIP_CHECKPOINT.md](hardware/WIP_CHECKPOINT.md) for checks, limitations and the main-only commit/push procedure.

## Historical firmware verification (0.1.5)

The `0.1.5-DRAFT` motor/output change passes 50/50 host tests (all 40 actual baseline tests plus 10 new boundary/fault-injection groups). GPIO17 now distinguishes constant LOW, PWM 1..800 and constant HIGH 1000; maximum switching frequency is 20kHz, and old unqualified NVS profiles cannot START. Normal STOP/thermal post-purge remains intact. See the [historical ESP32-S3 0.1.5 build kit](firmware/release/build-kits/0.1.5-esp32s3/) for target-build logs, images and reproducibility. The [older 0.1.4 kit](firmware/release/build-kits/0.1.4-esp32s3/PUBLICATION_RU.md) remains unchanged historical DRAFT evidence.

No physical ESP32, TFT, sensor, power stage, load or burner test has been performed. Building or flashing the image does not authorize fired operation.

## Selected power-return behavior

A complete loss of 24 V de-energizes the controller and loads. When power returns, the ESP32 boots and enters `RECOVERY_PURGE` with fuel and glow off. After the purge it waits for a new `START`; it does not resume interrupted operation automatically.

A separate 24 V monitor, TPS3808, extra `EN` reset path, `MAIN_POWER_OK` and GPIO43 assignment are excluded. USB is a service interface, not a backup supply. Existing STOP, watchdog, hardware latch, fuel-safety and cooling requirements remain in force. Separate draft sheets are retained as work history, not approved production nodes.

## Missing production work

Routing, schematic/PCB parity, power-stage qualification, Gerber, Drill, CPL, orderable BOM, physical testing and fired-operation approval remain incomplete. Do not connect loads or operate a burner from this unfinished design. See [docs/CURRENT_STATE_RU.md](docs/CURRENT_STATE_RU.md).

## License

No license is granted for the project's own code at this stage. Bundled third-party components retain their own licenses; see the [build-kit publication notes](firmware/release/build-kits/0.1.4-esp32s3/PUBLICATION_RU.md). Third-party manuals and scans are not redistributed; source documents are linked only.
