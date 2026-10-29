#!/usr/bin/env python3
"""Export every native KiCad PCB pad-connected component using pcbnew."""

from __future__ import annotations

import json
import sys
from pathlib import Path

import pcbnew


def pad_row(pad: pcbnew.PAD) -> dict[str, object]:
    footprint = pad.GetParentFootprint()
    position = pad.GetPosition()
    return {
        "uuid": pad.m_Uuid.AsString(),
        "reference": footprint.GetReference(),
        "pad": pad.GetNumber(),
        "net": pad.GetNetname(),
        "x_mm": position.x / 1_000_000,
        "y_mm": position.y / 1_000_000,
    }


def main() -> int:
    if len(sys.argv) != 3:
        raise SystemExit(f"usage: {sys.argv[0]} BOARD.kicad_pcb OUTPUT.json")
    source, output = map(Path, sys.argv[1:])
    board = pcbnew.LoadBoard(str(source))
    connectivity = board.GetConnectivity()
    if not connectivity.Build(board):
        raise RuntimeError("KiCad connectivity build failed")

    pads = list(board.GetPads())
    by_uuid = {pad.m_Uuid.AsString(): pad for pad in pads}
    groups: dict[tuple[str, ...], list[pcbnew.PAD]] = {}
    for pad in pads:
        connected = [item for item in connectivity.GetConnectedItems(pad) if isinstance(item, pcbnew.PAD)]
        connected.append(pad)
        uuids = tuple(sorted({item.m_Uuid.AsString() for item in connected}))
        groups[uuids] = [by_uuid[uuid] for uuid in uuids]

    rows = []
    for uuids in sorted(groups):
        members = sorted((pad_row(pad) for pad in groups[uuids]), key=lambda row: (str(row["reference"]), str(row["pad"]), str(row["uuid"])))
        nets = sorted({str(row["net"]) for row in members})
        rows.append({"group_id": uuids[0], "nets": nets, "pads": members})

    payload = {
        "kicad_version": pcbnew.GetBuildVersion(),
        "source": source.name,
        "pad_count": len(pads),
        "group_count": len(rows),
        "unconnected_count": connectivity.GetUnconnectedCount(False),
        "groups": rows,
    }
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(payload, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({key: payload[key] for key in ("pad_count", "group_count", "unconnected_count")}))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
