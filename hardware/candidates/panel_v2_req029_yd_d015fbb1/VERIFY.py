"""Run beside the delivered PCB/reports with Python 3; no extra packages required."""
from collections import Counter
import hashlib
import json
from pathlib import Path
import re
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parent
NAME = 'Sirokko26201_control_motor_candidate'
UUID = b'308c7e53-2925-4daf-804c-8412d9acad30'
BASE_SHA = 'c5e5b790270ca271102dde09e0d965ec33b5fbe3f2f5d9fbaa3fa1e93d35df3f'
FINAL_SHA = 'd015fbb1e4eea7caec96bf9dd34dcf0220e50bde06f0fbe3e7eed67ecfeaf3a3'


def canonical(value):
    return json.dumps(value, sort_keys=True, separators=(',', ':'))


def records(values):
    return Counter(map(canonical, values))


def unconnected_by_net(report):
    return Counter(tuple(sorted({re.search(r'\[([^]]+)\]', item['description'])[1]
                                 for item in violation['items']}))
                   for violation in report['unconnected_items'])


def check():
    pcb = (ROOT / (NAME + '.kicad_pcb')).read_bytes()
    assert hashlib.sha256(pcb).hexdigest() == FINAL_SHA
    assert pcb.count(UUID) == 1
    pos = pcb.index(UUID)
    start = pcb.rfind(b'\n\t(gr_text ', 0, pos)
    end = pcb.index(b'\n\t)\n', pos)
    text = pcb[start:end]
    assert b'(layer "B.Fab")' in text
    assert b'(at 94.43 61.67 90)' in text
    marker = b'\n\t\t\t(justify mirror)'
    assert text.count(marker) == 1
    # Removing the sole authorized property reconstructs the byte-exact Lab baseline.
    restored = pcb[:start] + text.replace(marker, b'', 1) + pcb[end:]
    assert hashlib.sha256(restored).hexdigest() == BASE_SHA

    before = json.loads((ROOT / 'BEFORE_DRC.json').read_text())
    after = json.loads((ROOT / (NAME + '-drc.json')).read_text())
    assert before['kicad_version'] == after['kicad_version'] == '10.0.7'
    old_type = 'nonmirrored_text_on_back_layer'
    assert Counter(v['type'] for v in before['violations']) == {
        old_type: 1, 'lib_footprint_mismatch': 2, 'track_dangling': 8}
    assert Counter(v['type'] for v in after['violations']) == {
        'lib_footprint_mismatch': 2, 'track_dangling': 8}
    assert records(v for v in before['violations'] if v['type'] != old_type) == records(after['violations'])
    assert all(v['severity'] == 'warning' for v in after['violations'])
    assert len(before['unconnected_items']) == len(after['unconnected_items']) == 84
    assert all(v['severity'] == 'error' for v in after['unconnected_items'])
    assert unconnected_by_net(before) == unconnected_by_net(after)
    assert before['ignored_checks'] == after['ignored_checks']
    assert len(after['ignored_checks']) == 5
    assert before['included_severities'] == after['included_severities']

    svg = ET.fromstring((ROOT / 'B_SIDE_DRAFT.svg').read_bytes())
    shapes = Counter(e.tag.rsplit('}', 1)[-1] for e in svg.iter())
    assert shapes['path'] > 100 and shapes['circle'] > 100
    assert any('YD-ESP32-S3 N16R8 V1.4' in (e.text or '') for e in svg.iter())
    result = {
        'status': 'PASS', 'pcb_sha256': FINAL_SHA,
        'baseline_sha256_reconstructed': BASE_SHA,
        'only_board_change': '(justify mirror) for ' + UUID.decode(),
        'all_other_pcb_bytes_unchanged_including_fills': True,
        'old_warnings_unchanged': 10, 'new_warning_absent': old_type,
        'unconnected_errors': 84, 'unconnected_counts_by_net_unchanged': True,
        'unconnected_item_representatives_identical': records(before['unconnected_items']) == records(after['unconnected_items']),
        'ignored_project_checks_unchanged': after['ignored_checks'],
        'drc_date': after['date'], 'kicad_version': after['kicad_version'],
        'svg_shapes': dict(shapes), 'release_status': 'DRAFT / NOT FOR ORDER / NO FIRE',
    }
    print(json.dumps(result, ensure_ascii=False, indent=2))


if __name__ == '__main__':
    check()
