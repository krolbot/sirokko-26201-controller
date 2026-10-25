# Geometry change register — MOSI partial, GND-repaired

## Accepted working PCB
`Sirokko26201_control_spi_MOSI_partial_GNDrepaired.kicad_pcb`

### Relative to preserved rejected partial
`Sirokko26201_control_spi_MOSI_partial_U9_J26.kicad_pcb` is retained unchanged as a restorable source.

- Removed the rejected six-segment F.Cu-only `/TC_SDI_MOSI` route from U9.12 to J26.4.
- Added `/TC_SDI_MOSI`:
  - F.Cu: `(27.1375,31.65)` → `(25.8,31.65)`.
  - via F.Cu↔B.Cu at `(25.8,31.65)`, Ø0.6 / drill 0.3 mm.
  - B.Cu: `(25.8,31.65)` → `(26.5,31.65)` → `(26.5,33.8)`.
  - via F.Cu↔B.Cu at `(26.5,33.8)`, Ø0.6 / drill 0.3 mm.
  - F.Cu: `(26.5,33.8)` → `(26.5,36)` → `(12,36)` → `(12,82.62)` → `(16,82.62)`.
- No footprint, net assignment, pad number, power, analog, rule, or ignored-check change.
- Native zone refill was saved after the final geometry.

## GND diagnosis and correction

Native `ZONE_FILLER.Fill`, `BuildConnectivity`, and `GetConnectedItems` after refill gave:

| PCB state | U9.1/U9.14/C9.2/C10.2 | C13.2 | all five common |
|---|---|---|---|
| baseline 176/47 | one component | same component | true |
| rejected partial | one component | separate isolated F.Cu GND island | false |
| accepted repaired partial | one component | same component | true |

The historical confirmation error was treating the zone identity/count and the unchanged aggregate DRC/unconnected count as proof of ground continuity. The rejected partial's local F.Cu MOSI escape severed the filled-GND connection that joined C13.2 to the U9/C9/C10 group. The accepted reroute moves its local crossing to B.Cu and returns to F.Cu at `(26.5,33.8)`, preserving the GND neck; it does not add a speculative GND stitch or alter rules.

## Scope status

Verified MOSI partial is only `U9.12 ↔ J26.4`. `U10.12` and `J1.17` remain separately unrouted; no claim of full MOSI completion or frequency/manufacturing qualification is made.
