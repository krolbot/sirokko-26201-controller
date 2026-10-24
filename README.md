# Sirokko 262.01 controller

Public DRAFT-01 snapshot of the controller-interface design and bench firmware for a Sirokko 262.01 heater retrofit. Russian documentation: [README_RU.md](README_RU.md).

This repository is not a finished burner controller. It contains the editable KiCad interface schematic, its PDF/netlist/ERC exports, project design documents, and an uncommissioned ESP-IDF bench firmware with host tests and checksum-pinned binaries. It does not contain a released PCB, Gerbers, a manufacturing BOM, physically verified firmware, or proof of combustion safety.

Do not connect 24 V loads, the glow plug, motor, fuel-clutch coil, or a live burner directly to the published logic interfaces. Independent hardware interlocks and verified power stages are still required.

No license is granted by this repository at this stage. Third-party manuals and scans are not redistributed; the design documents contain source links only.
