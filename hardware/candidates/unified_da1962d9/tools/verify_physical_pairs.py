#!/usr/bin/env python3
import hashlib
import itertools
import json
import sys

import pcbnew

pcb_path, output_path = sys.argv[1:3]
board = pcbnew.LoadBoard(pcb_path)
connectivity = board.GetConnectivity()
connectivity.Build(board)
pads = list(board.GetPads())

parent = {p.m_Uuid.AsString(): p.m_Uuid.AsString() for p in pads}


def find(item):
    while parent[item] != item:
        parent[item] = parent[parent[item]]
        item = parent[item]
    return item


def union(left, right):
    left_root, right_root = find(left), find(right)
    if left_root != right_root:
        parent[right_root] = left_root


pad_by_uuid = {p.m_Uuid.AsString(): p for p in pads}
for pad in pads:
    pad_uuid = pad.m_Uuid.AsString()
    for connected in connectivity.GetConnectedItems(pad):
        if isinstance(connected, pcbnew.PAD):
            union(pad_uuid, connected.m_Uuid.AsString())

groups = {}
for pad_uuid in parent:
    groups.setdefault(find(pad_uuid), []).append(pad_uuid)
partition = sorted((sorted(group) for group in groups.values()), key=lambda group: group[0])
pairs = sorted(
    pair
    for group in partition
    for pair in itertools.combinations(group, 2)
)
pairset_json = json.dumps(pairs, separators=(",", ":"))

pad_data = {}
for pad_uuid, pad in pad_by_uuid.items():
    footprint = pad.GetParentFootprint()
    position = pad.GetPosition()
    size = pad.GetSize()
    drill = pad.GetDrillSize()
    pad_data[pad_uuid] = {
        "ref": footprint.GetReference(),
        "pin": str(pad.GetNumber()),
        "net": pad.GetNetname(),
        "xy_mm": [pcbnew.ToMM(position.x), pcbnew.ToMM(position.y)],
        "size_mm": [pcbnew.ToMM(size.x), pcbnew.ToMM(size.y)],
        "drill_mm": [pcbnew.ToMM(drill.x), pcbnew.ToMM(drill.y)],
        "F_Cu": bool(pad.IsOnLayer(pcbnew.F_Cu)),
        "B_Cu": bool(pad.IsOnLayer(pcbnew.B_Cu)),
    }

result = {
    "pcb_path": pcb_path,
    "pads": pad_data,
    "partition": partition,
    "group_count": len(partition),
    "pair_count": len(pairs),
    "pairset_sha256": hashlib.sha256(pairset_json.encode()).hexdigest(),
    "save_board_called": False,
}
with open(output_path, "w", encoding="utf-8") as stream:
    json.dump(result, stream, ensure_ascii=False, indent=2, sort_keys=True)
print(json.dumps({key: result[key] for key in ("group_count", "pair_count", "pairset_sha256", "save_board_called")}, indent=2))
