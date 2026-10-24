# Firmware draft

ESP-IDF 5.5.2 firmware for ESP32-S3-DevKitC-1-N8R8 v1.1. The Russian documentation is in [README_RU.md](README_RU.md); the machine-readable pin contract is in [docs/io-contract.json](docs/io-contract.json).

This is an uncommissioned bench build, not authorization for fired operation. A fresh image fails closed with `FAULT_CONFIG`; measured timing, temperature, fan and flame parameters must pass validation and explicit token acceptance before the state machine can arm. External power supplies, the 24 V glow plug and load drivers are outside the logic board and must not route load current through ESP32 logic.

Verification performed for this snapshot:

- host simulation: 31/31 tests passed, including the coupled asynchronous latch trace and watchdog falling-edge budget;
- clean official `espressif/idf:v5.5.2` container build passed;
- ESP32-S3 image checksum and validation hash passed.

The current 0.1.1 bench artifacts and manifest are under `release/firmware/0.1.1/`. The original 0.1.0 artifact set remains unchanged under `release/firmware/`.
