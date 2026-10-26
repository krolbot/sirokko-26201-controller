"""Read-only KiCad 10 pcbnew audit: python3 NATIVE_AUDIT.py BASE_PCB CURRENT_PCB."""
from collections import Counter
import hashlib
from itertools import combinations
import json
from pathlib import Path
import sys

import pcbnew

BASE_SHA = 'd015fbb1e4eea7caec96bf9dd34dcf0220e50bde06f0fbe3e7eed67ecfeaf3a3'
FINAL_SHA = '115e8ed2d2929598fa6529c871c49247c11173040c0df65da872fd5922b8aeec'


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def uid(item):
    return item.m_Uuid.AsString()


def xy(point):
    return (point.x, point.y)


def inspect(path):
    board = pcbnew.LoadBoard(str(path))
    assert board and board.BuildConnectivity(), 'BuildConnectivity failed'
    connectivity = board.GetConnectivity()
    pads = list(board.GetPads())
    tracks = list(board.GetTracks())
    zones = [z for z in board.Zones() if not z.GetIsRuleArea()]
    names = {uid(p): f'{p.GetParentFootprint().GetReference()}.{p.GetNumber()}@{uid(p)}'
             for p in pads}
    pad_geometry = {
        uid(p): (p.GetParentFootprint().GetReference(), p.GetNumber(), p.GetNetname(),
                 xy(p.GetPosition()), xy(p.GetSize()), xy(p.GetDrillSize()),
                 p.GetShape(), p.GetOrientationDegrees(), p.GetLayerSet().FmtBin())
        for p in pads
    }
    copper = {
        uid(t): (t.GetClass(), t.GetNetname(), xy(t.GetStart()), xy(t.GetEnd()),
                 t.GetLayerSet().FmtBin(),
                 tuple((layer, t.GetWidth(layer)) for layer in t.GetLayerSet().Seq())
                 if isinstance(t, pcbnew.PCB_VIA) else t.GetWidth(),
                 t.GetDrillValue() if isinstance(t, pcbnew.PCB_VIA) else None)
        for t in tracks
    }
    groups = set()
    pairs = set()
    for pad in pads:
        group = frozenset(uid(i) for i in connectivity.GetConnectedItems(pad)
                          if uid(i) in names) | {uid(pad)}
        groups.add(group)
        pairs.update(combinations(sorted(group), 2))

    # IGNORE_NETS uses physical clusters, so a wrong-net contact is not hidden by net filtering.
    seen = set()
    shorts = set()
    physical_queries = 0
    for item in pads + tracks + zones:
        if uid(item) in seen:
            continue
        cluster = list(connectivity.GetConnectedItems(item, pcbnew.IGNORE_NETS))
        physical_queries += 1
        seen.update(uid(i) for i in cluster)
        nets = {i.GetNetname() for i in cluster if i.GetNetCode() > 0}
        if len(nets) > 1:
            shorts.add(tuple(sorted((uid(i), i.GetNetname()) for i in cluster)))
    return {
        'board': board, 'names': names, 'pad_geometry': pad_geometry,
        'copper': copper, 'groups': groups, 'pairs': pairs, 'shorts': shorts,
        'summary': {'pads': len(pads), 'footprints': len(list(board.GetFootprints())),
                    'segments': sum(not isinstance(t, pcbnew.PCB_VIA) for t in tracks),
                    'vias': sum(isinstance(t, pcbnew.PCB_VIA) for t in tracks),
                    'zones': len(list(board.Zones())), 'copper_zones': len(zones),
                    'native_pad_groups': len(groups), 'connected_pad_pairs': len(pairs),
                    'cross_net_clusters': len(shorts), 'physical_cluster_queries': physical_queries},
    }


base_path, final_path = map(Path, sys.argv[1:3])
assert sha(base_path) == BASE_SHA and sha(final_path) == FINAL_SHA
before, after = inspect(base_path), inspect(final_path)
assert before['pad_geometry'] == after['pad_geometry'], 'pad identity/geometry/net changed'
assert before['copper'].keys() <= after['copper'].keys(), 'old copper item removed'
assert all(after['copper'][k] == v for k, v in before['copper'].items()), 'old copper changed'
assert len(before['copper']) == 929
added_ids = after['copper'].keys() - before['copper'].keys()
added = [t for t in after['board'].GetTracks() if uid(t) in added_ids]
expected = Counter([
    ('/+3V3', (32937500, 92000000), (35225000, 92000000)),
    ('/GND', (31062500, 91050000), (36775000, 91050000)),
    ('/GND', (36775000, 91050000), (36775000, 92000000)),
])
assert Counter((t.GetNetname(), xy(t.GetStart()), xy(t.GetEnd())) for t in added) == expected
assert all(t.GetClass() == 'PCB_TRACK' and t.GetLayer() == pcbnew.F_Cu
           and t.GetWidth() == 300000 for t in added)
lost_pairs = before['pairs'] - after['pairs']
assert not lost_pairs, 'previously connected pads disconnected'
assert not (after['shorts'] - before['shorts']), 'new cross-net physical cluster'
assert not after['shorts'], 'cross-net physical cluster present'
by_name = {name.split('@')[0]: key for key, name in after['names'].items()}
targets = {tuple(sorted((by_name[a], by_name[b])))
           for a, b in [('U7.3', 'C7.1'), ('U7.1', 'C7.2')]}
assert not (targets & before['pairs']), 'target was already connected in baseline'
assert targets <= after['pairs'], 'target local connection missing'
assert after['pairs'] - before['pairs'] == targets, 'unexpected additional pad connection'
assert after['summary']['segments'] == 753 and after['summary']['vias'] == 179
assert sha(base_path) == BASE_SHA and sha(final_path) == FINAL_SHA, 'input file changed'
result = {
    'status': 'PASS', 'release_status': 'DRAFT / NOT FOR ORDER / NO FIRE',
    'kicad_version': pcbnew.GetBuildVersion(), 'read_only': True,
    'baseline_pcb_sha256': BASE_SHA, 'current_pcb_sha256': FINAL_SHA,
    'native_api': ['BOARD.BuildConnectivity', 'CONNECTIVITY_DATA.GetConnectedItems',
                   'CONNECTIVITY_DATA.GetConnectedItems(IGNORE_NETS)'],
    'before': before['summary'], 'after': after['summary'],
    'old_copper_objects_preserved': len(before['copper']),
    'pad_identity_geometry_nets_preserved': True, 'lost_connected_pad_pairs': [],
    'new_connected_pad_pairs': [[after['names'][p] for p in pair] for pair in sorted(targets)],
    'new_cross_net_shorts': [], 'input_hashes_unchanged_after_native_read': True,
    'added_segments': [{'uuid': uid(t), 'net': t.GetNetname(), 'layer': 'F.Cu',
                        'width_iu': t.GetWidth(), 'start_iu': xy(t.GetStart()),
                        'end_iu': xy(t.GetEnd())} for t in added],
    'before_pad_groups': sorted(sorted(before['names'][p] for p in group)
                                for group in before['groups']),
    'after_pad_groups': sorted(sorted(after['names'][p] for p in group)
                               for group in after['groups']),
    'limitations': ['Local U7/C7 connections only; main +3V3/GND remain open.',
                    'Not electrical, manufacturing or fire approval.',
                    'KiCad startup PROPERTY_ENUM assertions are retained in NATIVE_STDERR.txt; no suppression or server fix.'],
}
print(json.dumps(result, ensure_ascii=False, indent=2))
