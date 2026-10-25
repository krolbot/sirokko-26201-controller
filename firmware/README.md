# Sirokko 262.01 — 0.1.6-DRAFT / BENCH ONLY

ESP-IDF 5.5.2 firmware for ESP32-S3-DevKitC-1-N8R8 v1.1. This is an uncommissioned bench candidate, not authorization for fired operation. No physical board was connected or flashed.

The authoritative operator/build/limitations guide is [README_RU.md](README_RU.md). See [reference matrix](docs/reference_features_ru.md), [buttons and screens](docs/operator_ui_ru.md), [commissioning](docs/commissioning_ru.md), [service/OTA API](docs/service_network_ru.md), and the unchanged [pin contract](docs/io-contract.json).

Version 0.1.6 adds a qualified-profile thermostat, Russian TFT menu, calibration/command accounting/history, deferred validated NVS persistence, and separately authorized optional TLS MQTT/HTTPS signed OTA. It does not add remote ignition, automatic restart, independent pump metering, or invented measurements. Production control/runtime/adapters are exercised in 93 C++ groups plus 4 offline service-tool tests; peripheral/RTOS/TLS/flash boundaries are injected, not physical observations.

The reproducible candidate kit is [release/build-kits/0.1.6-esp32s3/](release/build-kits/0.1.6-esp32s3/), including full source identity, real binaries, configuration, logs, simulated render images, manifest and SHA256SUMS. Older kits remain unchanged. Initial migration from the old factory-only partition layout requires USB and the NEW flash_args: the application is now at 0x20000, not 0x10000. Never flash a running heater or erase critical NVS as an automatic repair.
