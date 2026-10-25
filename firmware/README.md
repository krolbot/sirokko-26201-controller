# Firmware draft

ESP-IDF 5.5.2 firmware for ESP32-S3-DevKitC-1-N8R8 v1.1. The Russian documentation is in [README_RU.md](README_RU.md); the machine-readable pin contract is in [docs/io-contract.json](docs/io-contract.json).

This is an uncommissioned bench build, not authorization for fired operation. A fresh image or legacy schema-1 NVS fails closed with `FAULT_CONFIG`; it cannot command the fan until a PWM-only schema-2 profile is accepted. Every accepted boot/reset enters `RECOVERY_PURGE`, keeps fuel and glow off, and requires the full commissioned purge time plus valid cold sensor readings before `WAIT_COOLDOWN` or `LOCKOUT`. Latched faults persist in NVS until a qualified manual RESET. Measured timing, temperature, PWM and flame parameters must pass validation and explicit token acceptance before the state machine can arm. The supply, glow plug, and on-board power stages are not yet qualified; load current must stay in the power section of the common PCB and never pass through ESP32 logic.

A separate 220/24 V presence input, TPS3808, and an extra `EN` reset path are explicitly excluded; `MAIN_POWER_OK` and GPIO43 are not used. In normal operation the ESP32 is powered from the common 24 V board, so a complete power loss produces a real boot/reset on return and enters `RECOVERY_PURGE`. Keeping the MCU alive from USB while 24 V is absent is not a supported operating-heater mode.

Verification performed for this source snapshot:

- current host tests: 50/50 passed, including all 40 tests found in the actual main baseline and 10 motor/output/NVS/CLI boundary and failure-injection groups;
- real production adapter sources `output_driver.cpp` and `commissioning.cpp` are compiled against host IDF API stubs; this is not electrical hardware feedback;
- target build evidence and flashing images are recorded separately in the versioned build kit.

The selected local UI is now the removable GMT020-02-7P ST7789V 240x320 TFT. It shares SPI2 MOSI/SCK with both MAX31856 devices while retaining independent chip selects, mode and clock settings. The TFT has no MISO/TE/presence signal, so software can detect driver/transfer failures but cannot prove that an unplugged physical panel received pixels.

Version 0.1.5-DRAFT restricts the LTC7001 motor candidate to 77..20000 Hz for the selected APB 80MHz/10bit configuration; requested 0 is constant LOW, 1..800 is PWM and 1000 is constant HIGH via `ledc_stop(..., 1)`. Duty 801..999 is rejected at configuration, CLI acceptance and output boundaries. High-speed machine states are not clamped. A failing output API latches the adapter OFF until reboot, inhibits fuel/glow/permit/ARM/heartbeat, asserts SW_TRIP, and independently detaches GPIO17 from LEDC. Normal STOP and thermal-fault post-purge remain unchanged.

The current reproducible bench set is `release/build-kits/0.1.5-esp32s3/`. Earlier 0.1.4 and older artifacts remain unchanged and must not be used as evidence for this version.
