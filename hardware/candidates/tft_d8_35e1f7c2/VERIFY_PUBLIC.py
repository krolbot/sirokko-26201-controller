"""Read-only stdlib check of the public snapshot, not a new KiCad acceptance."""
from collections import Counter
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parent


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def report(name):
    return json.loads((ROOT / 'reports' / name).read_text())


def main():
    sums = {}
    for line in (ROOT / 'SHA256SUMS.txt').read_text().splitlines():
        digest, name = line.split('  ', 1)
        assert name not in sums and not Path(name).is_absolute() and '..' not in Path(name).parts
        assert sha(ROOT / name) == digest, name
        sums[name] = digest
    actual = {p.relative_to(ROOT).as_posix() for p in ROOT.rglob('*') if p.is_file()}
    assert actual == set(sums) | {'SHA256SUMS.txt'}, actual ^ set(sums)
    manifest = json.loads((ROOT / 'PUBLIC_MANIFEST.json').read_text())
    for item in manifest['files']:
        assert sha(ROOT / item['path']) == item['public_sha256'], item['path']
        if item['transformation'] == 'none':
            assert item['public_sha256'] == item['source_sha256'], item['path']
    for item in manifest['documents']:
        assert sha(ROOT.parents[2] / item['path']) == item['sha256'], item['path']
    assert sha(ROOT / 'Sirokko26201_control_motor_candidate.kicad_pcb') == manifest['accepted_pcb_sha256']
    for item in report('CONTEXT.json'):
        assert sha(ROOT / item['path']) == item['sha256'], item['path']
    before, after, active = [report(n + '_NATIVE.json') for n in ('BEFORE', 'AFTER', 'ACTIVE')]
    assert before['pairs'] == after['pairs'] == active['pairs']
    assert before['groups'] == after['groups'] == active['groups']
    assert len(after['pairs']) == 5383 and len(after['groups']) == 240
    assert not after['shorts'] and not active['shorts']
    assert all(after[k] == active[k] for k in ('pads', 'copper', 'zones'))
    def violations(data):
        return Counter((v['type'], v['severity'], tuple(sorted(i['uuid'] for i in v.get('items', [])))) for v in data['violations'])
    baseline = report('BEFORE_DRC.json')
    for name in ('BEFORE_DRC.json', 'DRC.json', 'ACTIVE_DRC.json'):
        data = report(name)
        assert len(data['unconnected_items']) == 82
        assert len(data['violations']) == 10 and all(v['severity'] == 'warning' for v in data['violations'])
        assert violations(data) == violations(baseline)
        assert data['ignored_checks'] == baseline['ignored_checks']
    geometry = report('GEOMETRY.json')
    assert geometry['covered'] == 4 and len(geometry['checks']) == 4
    assert not geometry['copper_collisions'] and not any(geometry['old_center_keepouts'].values())
    for zone in geometry['checks']:
        assert zone['covered'] and zone['convex'] and zone['vertices'] == 64
        assert zone['min_edge_distance_mm'] >= 4 and zone['layers'] == ['F.Cu', 'B.Cu']
        assert zone['flags'] == [True, True, True]
    negative = report('NEGATIVE_TEST.json')
    assert negative['covered'] == 3 and negative['saved'] is False
    assert not report('MECHANICAL_CHECK.json')['EP_direct_filled_plane_contacts']
    print(f'PASS_PUBLIC_SNAPSHOT: {len(sums)} hashes; 4/4 zones; 5383 pairs; 10 warnings / 82 unconnected; DRAFT / NOT FOR ORDER / NO FIRE')


if __name__ == '__main__':
    main()
