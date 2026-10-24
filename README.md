# Sirokko 262.01 controller

> **DRAFT / BENCH ONLY / NOT FOR ORDER.** This branch publishes the accepted `0.1.4-draft` firmware. It is not a finished burner controller or a PCB manufacturing release. Russian documentation: [README_RU.md](README_RU.md).

## Published in this snapshot

- `firmware/` — ESP-IDF 5.5.2 sources, host tests, I/O contract, and the complete `0.1.4-draft` flashing set;
- `docs/` — current functional requirements, including the selected power-return behavior;
- `hardware/kicad/`, `renders/`, and `verification/` — the **previous DRAFT-01 hardware revision**, retained for history only.

The current Lab PCB and ongoing `motor_pwm_REBUILD` work are deliberately not copied into this branch. The retained hardware revision is outdated, incomplete, and must not be used to order or assemble a board.

## Firmware verification

The accepted `0.1.4-draft` set passed 39/39 host tests and a clean ESP-IDF 5.5.2 build. Release checksums and ESP image validation are included in `firmware/release/firmware/0.1.4/`.

No physical ESP32, TFT, sensor, power stage, load, or burner test has been performed. Building or flashing the image does not authorize fired operation.

## Selected power-return behavior

A complete loss of 24 V de-energizes the controller and loads. When power returns, the ESP32 boots and enters `RECOVERY_PURGE` with the clutch/fuel channels and glow plug off. After the purge it waits for a new `START`; it does not resume the interrupted `RUN` or ignition attempt automatically.

A separate 24 V monitor, TPS3808, extra `EN` reset path, `MAIN_POWER_OK`, and GPIO43 assignment are excluded. USB is a service interface, not a backup supply. Existing STOP, watchdog, hardware latch, fuel-safety, and cooling requirements remain in force.

## Missing production work

There is no current published PCB, completed routing, closed DRC, Gerber, Drill, CPL, orderable BOM, physical qualification, or fired-operation approval. See [docs/STATUS_RU.md](docs/STATUS_RU.md) for the exact boundary.

## License

No `LICENSE` file is present. No permission to copy, modify, or distribute is granted automatically. Third-party manuals are linked but not redistributed.
