# SPI routing verification

Status: partial physical routing; not production release.

Accepted copper:
- /TC_EXH_CS: continuous copper U9.9 (27.1375,29.70) -> J1.16 (105.86,73.10), 0.25 mm, one 0.60/0.30 mm via at (24.50,31.50).

Not closed:
- R2.2 pull-up is still not connected to /TC_EXH_CS.
- /TC_SCK, /TC_SDI_MOSI, /TC_SDO_MISO, /TC_EXH_DRDY are unchanged.

Validation after fill_zones:
- DRC: 4 warnings only, unchanged lib_footprint_mismatch at J23, C1, J24, J25.
- No new error types: no short, clearance, tracks_crossing, soldermask_bridge, or via_dangling.
- Board inventory: 88 footprints, 137 traces, 36 vias, 90 nets, 3 zones.
- DRC reports 180 unconnected items; /TC_EXH_CS remains incomplete because R2.2 is not connected.

Artifacts:
- drc_final_partial_spi/
- svg_f_cu/, svg_b_cu/, png_top/, png_bottom/
