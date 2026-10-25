# Sirokko SPI — состояние новой копии

**Статус:** WIP, не для производства.

## Закрыто
- `/TC_EXH_CS`: U9.9 → J1.16 (существующая проверенная медь) и R2.2 подключены; R2 перенесён без поворота на origin `(14.34, 46.43)`, pad 2 `(24.50, 46.43)` непосредственно на существующий сегмент `/TC_EXH_CS`.
- Подтяжка R2.1 `/+3V3` подключена B.Cu дорожкой к C13.1 через via `(21.725, 29.05)`.

## Ожидают разводки
`/TC_SCK`, `/TC_SDI_MOSI`, `/TC_SDO_MISO`, `/TC_EXH_DRDY`, `/TC_AIR_DRDY`.

## Проверка финального файла
- PCB SHA-256: `090baef6e1c599ead63427344a430f65fcdf9f9d18d29bb206b0bc6d5ab87e09`
- KiCad 10.0.7/MCP: 88 footprints, 138 traces, 37 vias, 90 nets, 3 zones.
- После fill zones полный DRC: 4 исходных предупреждения `lib_footprint_mismatch` (J23, C1, J24, J25), 178 unconnected; новых shorts/clearance/crossing/dangling нет.
- Five baseline ignored checks were not modified.

Файл намеренно не объявляется полной SPI-разводкой или производственно готовым.
