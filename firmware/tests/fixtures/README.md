# Legacy NVS fixture

`accepted-draft05-config.bin` is the actual `config` NVS blob serialized and read back as accepted by the unmodified schema-3 CommissioningStore and complete test_config at baseline `27bc2ea110e918911c5a94dcf0d3426cc9695d49`. SHA256: `df12509df84ca282529a917899c0e5b00ef66849bfffe291a9afbc94f7453081`.

Generator: `make_draft05_fixture.cpp`. To regenerate, export that baseline's firmware/components, firmware/main and firmware/tests with git archive into a separate directory; compile the generator with its include paths plus baseline control.cpp, button.cpp, operator.cpp, output_driver.cpp, commissioning.cpp and idf_stubs.cpp using g++ -std=c++20. Run it with the fixture output path. It checks legacy validation/save/load acceptance before exporting the blob. The fixture is a host ABI serialization (same layout used by this project's host NVS tests), not a captured physical ESP32 NVS partition and not a deployable commissioning profile.

The DRAFT-06 test loads these real legacy-layout bytes through the new store, verifies that acceptance is not migrated and the original record remains unchanged, then verifies GPIO8 stays released. No RUN state or credentials are present.
