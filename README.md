# Sirokko 262.01 controller

> **DRAFT / BENCH ONLY / NOT FOR ORDER.** Main contains separate hardware WIP snapshots and `0.1.8-DRAFT` bench firmware. They are not one finished burner controller or a PCB manufacturing release. Russian documentation: [README_RU.md](README_RU.md).

## Current requirements and status

**Latest PCB checkpoint: [TFT power feed PCB 965b97be](hardware/candidates/tft_power_feed_965b97be_wip/README_RU.md).** The PCB is published byte-exact with its WIP report, recorded DRC, physical-connectivity comparison, DC screening and labeled F.Cu/B.Cu detail. The existing SCH/PRO, local libraries and firmware are unchanged. Recorded DRC is 229 unconnected items and 30 existing warnings — **not PASS**. TFT_3V3/J26/signal routing and physical/power testing remain incomplete. **DRAFT / NOT FOR ORDER / NO POWER / NO FIRE.**

**Previous complete native hardware base: [panel Harwin/BOM PCB 4fe3df0d](hardware/candidates/panel_harwin_4fe3df0d_wip/README_RU.md).** The byte-exact seven-sheet KiCad snapshot integrates J26 Harwin M20-9960845 on B.Cu, C28/R122/R123 selections and current K1–K3 relay metadata; its full BOM and explicitly labeled two-side DRAFT preview are included. The previous immutable candidates remain history and are not overwritten.

This is the previous complete native base, not a fully accepted layout or a production release. Preserved copper is still incomplete; `full_layout_accepted=false`, `general_routing_allowed=false`, `firmware_integration_allowed=false`, `production_release=false`. No new DRC/ERC was run: historical 31 warnings / 246 unconnected / 0 parity and ERC 9 warnings remain source-revision records, not a fresh PASS. The [power and relay schematic review](docs/research/INPUT_POWER_4FE3_SCHEMATIC_REVIEW_RU.md) is a static source review, not bench qualification. **DRAFT / NOT FOR ORDER / NO POWER / NO FIRE.** Firmware is unchanged.

**Accepted assembly checkpoint:** [free PH2→J26 cable handoff](docs/checkpoints/free_cable_ph2_j26_04a852e3/HANDOFF_RU.md). The stock cable remains free, with `PH2 pin N -> J26 pin N`; the former exact XYZ route is not an acceptance criterion. The checkpoint also contains the current fixed J26/R16 Z-section and assembly contract. Physical assembly, soldering, electrical and safety checks remain open; `production_release=false`.

**Latest published hardware WIP: [panel-axis PCB 4a3ab23b + corrected relay-envelope WRL 19eb37e4](hardware/candidates/panel_axis_4a3ab23b_relay3d_19eb37e4/README_RU.md).** It is a separate immutable DRAFT snapshot after `t521_bdd026d5`, not a replacement for earlier history and not an accepted full layout. The package preserves the complete seven-sheet hierarchy, all 15 root local-library targets and the project-native CAD/library files; internal tools and the vendor STEP without confirmed redistribution permission are excluded.

**DRAFT / NOT FOR ORDER / NO POWER / NO FIRE.** The J23/J22/J24 actuator axis is corrected to X=64.95 mm, exactly 60.00 mm from the right window edge; the shared K1–K3 WRL Y-translation sign is also corrected. Full DRC still reports 31 warnings and 246 unconnected items; ERC reports 9 warnings. Relay height/Z, full TFT/cable mechanics, general routing and firmware remain HOLD; `full_layout_accepted=false`, `production_release=false`.

**2026-10-27 — selected TFT: [Waveshare 18366 / 2.4inch LCD Module](docs/research/WAVESHARE_18366_INTEGRATION_RU.md), ChipDip 8013572929, without touch.** This ready-made removable module replaces the 10684/GMT020 targets. Use the supplied cable from its installed PH2.0 connector to a compatible male header on Sirokko: eight separate female contacts at the free end, not one 8P housing. No header soldering on the TFT or direct board-to-board mating; four-hole/standoff mounting is retained. The YD-ESP32-S3 N16R8 USB-C is retained. The selection is represented in the latest frozen hardware candidate, but it is not a purchase or an accepted mechanical/panel integration. The carrier connector and mounting/panel Z-height stack, including the PH2.0 plug and cable bend, remain unaccepted. Use regulated 3.3 V supply/logic; there is no external MISO. General routing/firmware remain HOLD; `production_release=false`.

**Earlier published local WIP: [input-protection repair PCB f251bdfc / root SCH 7cf3ab36](hardware/candidates/input_f251bdfc/README_RU.md).** This frozen snapshot contains the complete seven-sheet KiCad project, all 14 project-local library dependencies, full reports and labeled full-board F.Cu/B.Cu SVG/PNG views. The native files are the verified repair package; earlier candidates remain immutable history, not parallel current PCBs.

**DRAFT / NOT FOR ORDER / NO FIRE.** The local repair removes the recorded 18 shorts, 8 clearance violations and 18 solder-mask bridges without changing the 5558 physical connectivity pairs relative to its direct CF907717 predecessor. The full DRC still reports 31 warnings, 226 unconnected errors and 14 schematic-parity warnings; ERC reports 9 warnings. J13.2/GND and the current RESET paths remain physically open. Full layout/mechanical Z, BOM, power-ground, thermal/startup/safety, bench validation, general routing and firmware are unaccepted; `production_release=false`. The firmware tree is unchanged by this publication.

**Historical checkpoint before the rectangular-board change:** [MPN_BOM_F1FFC0B3](hardware/candidates/MPN_BOM_F1FFC0B3/README.md) preserves the exact stepped native WIP after the edge-terminal and J12/J13 MPN corrections. It is not the current PCB. Component coordinates and the current 335 × 170 mm WIP size are not fixed design constraints; see the [Sirokko layout-freedom rule](docs/LAYOUT_FREEDOM_RU.md).

[REQ-029: right-hand control column](docs/UI_RIGHT_COLUMN_RU.md): TFT on the left; push rotary encoder, one combined START/STOP button and protection RESET on the right, top to bottom on one vertical axis. The accepted nominal distance is **60 mm from the right window edge to the axis**, with 30 mm vertical spacing. This is an assigned engineering dimension based on a hand-envelope model, not an anthropometric standard or a physical guarantee.

- [1:1 SVG sketch](docs/Sirokko_UI_right_column_1to1.svg) and [preview PNG](docs/Sirokko_UI_right_column_1to1.png): required relative geometry, not a drilling template.
- [Requirements register](docs/REQUIREMENTS_RU.md), [panel contract](docs/UX_PANEL_REQUIREMENTS_RU.md), [current state](docs/CURRENT_STATE_RU.md), [session decision recovery](docs/research/SESSION_DECISIONS_RECOVERY_RU.md) — selected/superseded decisions and the limits of the targeted history check, not a production release.
- [Earlier UI centres](docs/UI_LAYOUT_CENTRES_RU.md) and [TFT/BL placement](docs/TFT_BL_PANEL_WINDOW_RU.md): historical absolute coordinates. The old B-side view with controls on the left and RESET off-axis is superseded; frozen candidates remain unchanged.

### Earlier TFT snapshot — historical status, not PCB 14e676dd

**2026-10-26: TFT mounting keepout correction accepted, 4/4 only; general routing and firmware remain HOLD.** The immutable [TFT Ø8 snapshot 35e1f7c2](hardware/candidates/tft_d8_35e1f7c2/README_RU.md) corrects the earlier 0/4 defect with four F.Cu/B.Cu circumscribed 64-gon R4 keepouts around the relocated MH5 holes. Ø8 is the protected area; the NPTH drill is Ø2.7 mm. The [dated erratum supplement](docs/TFT_KEEPOUT_ERRATUM_RU.md) supersedes the old unresolved-defect statement only for this PCB, not the withdrawal of general continuation permission. The [115e8ed2](hardware/candidates/u7_c7_local_115e8ed2/README_RU.md) and [d015fbb1](hardware/candidates/panel_v2_req029_yd_d015fbb1/README_RU.md) snapshots remain unchanged history.

The board still has **10 DRC warnings / 82 unconnected items**. LTC7001 EP-to-GND plane, D9002 and the real gate loop remain unaccepted; [MMBD1501A correction rationale](docs/D9002_BOOTSTRAP_CORRECTION_RU.md) is published, but the diode replacement is **not applied**. RESET is a separate operation; TFT/knob heights and YD power remain unqualified. The [Connfly manufacturer follow-up](docs/DS1023_MANUFACTURER_FOLLOWUP_RU.md) does not establish mating depth or authorize socket/PCB/standoff changes. **DRAFT / NOT FOR ORDER / NO FIRE.** This is not production approval or permission for general routing, firmware integration, manufacturing, flashing or physical operation. Firmware, canonical CAD and earlier snapshots are unchanged.

## Earlier requirements and status (historical)

The [unified motor + SPI working directory](hardware/candidates/unified_motor_spi_working/README_RU.md) is the source for continued routing of the common PCB. Earlier canonical files and separate candidates remain unchanged historical snapshots, not parallel active sources. The [current mounting and motor-logic WIP](docs/MOUNTING_AND_MOTOR_LOGIC_REVIEW_RU.md) has 405 segments / 82 vias, 158 unconnected items, 4 existing DRC violations and 5 ignored checks; the recorded DRC exit code is 5, not an overall PASS. Four mounting holes were moved and copper keepouts added; the board remains 180 × 170 mm. Local OCP-to-motor logic paths are routed, but common power and ground remain unfinished. OCP and Kelvin reports and images remain historical snapshots. The 194/52 motor + SPI baseline is preserved in commit [7ede3fca5f8371e4e3ec7252b4ba637994532fc2](https://github.com/krolbot/sirokko-26201-controller/commit/7ede3fca5f8371e4e3ec7252b4ba637994532fc2), with its historical reports and images retained separately. TFT supply connectivity is complete geometrically; the long EN route and 3.3 V budget are not qualified, and power routing and physical testing remain incomplete.

- [Requirements register](docs/REQUIREMENTS_RU.md) — all requirements, superseded decisions and acceptance criteria.
- [Panel and UX requirements](docs/UX_PANEL_REQUIREMENTS_RU.md) — separate START/STOP/protection RESET and a push rotary encoder; [historical button-only concept](docs/UX_PANEL_CONCEPT_HISTORY_RU.md), not an updated encoder mockup.
- [Current state and work plan](docs/CURRENT_STATE_RU.md) — published evidence, unfinished work and physical-test limits.
- [RTC and timer reference research](docs/RTC_TIMER_REFERENCE_RU.md) — pinned sources, battery-holder/RTC candidates and adaptation limits.
- [CR2032 holder selection](docs/CR_HOLDER_SELECTION_RU.md) — Keystone 1060 candidate, catalog/footprint geometry and polarity comparison, and remaining checks; not integrated, not FOR_ORDER.

These documents take precedence over historical requirements below. The mandatory four-button layout and combined START/STOP are superseded: the user approved separate START, STOP and protection RESET plus a push rotary encoder for navigation. The enclosure is user-designed; panel geometry, components and physical UX remain unqualified. Battery-backed RTC and Wi-Fi/MQTT/OTA remain requirements, without authorizing infrastructure deployment or calendar ignition. The [historical 0.1.8 DRAFT-06 kit](firmware/release/build-kits/0.1.8-esp32s3/) implements the four-button firmware and GPIO8 open-drain contract: real ESP-IDF target build exit 0, host 110/110 C++ + 4/4 Python, with separate RESET-skew models. It is dangerously incompatible with the existing external NC STOP wiring: do not flash the current PCB. Buttons/RTC remain physically unintegrated; NKK BB25AB-FA is only a functional RESET candidate, with ChipDip/footprint/full qualification HOLD. No physical tests or fired operation were authorized.

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
