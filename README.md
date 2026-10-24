# Sirokko 262.01 controller

Hardware WIP checkpoint: recovered main PCB and matching native project sources for KiCad 10.0.6. Russian documentation: [README_RU.md](README_RU.md).

This is not a released burner controller. `hardware/kicad/` contains the recovered working PCB, main schematic, local libraries and separate draft sheets. The input-protection candidate in `hardware/candidates/input-protection/` is not integrated into that PCB.

The checkpoint has 88 footprints, 123 track segments, 32 vias and 3 copper zones. ERC reports no violations under the recorded project settings; DRC still reports 4 library-footprint warnings and 182 unconnected items. A separate schematic-parity check reports 7 warnings. See [hardware/WIP_CHECKPOINT.md](hardware/WIP_CHECKPOINT.md) for results, limitations and the next commit/push procedure.

The PDF/netlist/ERC files directly under `renders/` and `verification/` are historical DRAFT-01 exports, not exports of the current PCB. Firmware is unchanged from the branch baseline; later firmware work is separate. No manufacturing package, physically verified firmware or proof of combustion safety is supplied.

Do not connect loads or operate a burner from this unfinished design. Independent interlocks, qualified power stages and bench/combustion testing remain required.

No license is granted at this stage. Third-party manuals and scans are not redistributed; source documents are linked only.
