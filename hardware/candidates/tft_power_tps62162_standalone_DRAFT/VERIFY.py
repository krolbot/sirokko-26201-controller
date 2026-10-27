#!/usr/bin/env python3
"""Verify this frozen DRAFT using only Python's standard library."""
import hashlib
import json
from pathlib import Path
import re
import struct
import xml.etree.ElementTree as ET

HERE = Path(__file__).resolve().parent
NAME = 'tft_power_tps62162_review_DRAFT'
manifest = json.loads((HERE / 'PUBLIC_MANIFEST.json').read_text())
assert manifest['integration_status'] == 'standalone_not_integrated'
assert manifest['production_release'] is False
assert manifest['source_capture']['before'] == manifest['source_capture']['after']
for name, info in manifest['files'].items():
    data = (HERE / name).read_bytes()
    assert len(data) == info['size'], name
    assert hashlib.sha256(data).hexdigest() == info['sha256'], name
for line in (HERE / 'SHA256SUMS').read_text().splitlines():
    digest, name = line.split('  ', 1)
    assert hashlib.sha256((HERE / name).read_bytes()).hexdigest() == digest, name
expected_files = {name for name in manifest['files'] if not name.startswith('../')}
assert {p.relative_to(HERE).as_posix() for p in HERE.rglob('*') if p.is_file()} == expected_files | {'PUBLIC_MANIFEST.json', 'SHA256SUMS'}
for line in (HERE / 'passives_v3/SHA256SUMS').read_text().splitlines():
    digest, name = line.split('  ', 1)
    assert hashlib.sha256((HERE / 'passives_v3' / name).read_bytes()).hexdigest() == digest, name
assert len(list((HERE / 'passives_v3').rglob('*.kicad_mod'))) == 5
edit = json.loads((HERE / 'FIELD_EDIT_VERIFICATION.json').read_text())
assert edit['reverse_authorized_changes_recovers_original_bytes']
assert edit['result_sha256'] == hashlib.sha256((HERE / (NAME + '.kicad_sch')).read_bytes()).hexdigest()
assert edit['footprints_assigned'] is False
native = json.loads((HERE / 'NATIVE_VERIFICATION.json').read_text())
assert native['full_pin_to_net_equal'] and native['ignored_checks_unchanged']
acceptance = json.loads((HERE / 'ENGINEER_ACCEPTANCE.json').read_text())
assert acceptance['status'] == 'GRAPHICAL_LAYOUT_ACCEPTED_NOT_PRODUCTION'
assert acceptance['schematic_sha256'] == hashlib.sha256((HERE / (NAME + '.kicad_sch')).read_bytes()).hexdigest()
assert acceptance['pdf_sha256'] == hashlib.sha256((HERE / (NAME + '.pdf')).read_bytes()).hexdigest()

expected = {
    '/+5V_CARRIER': {'U301.2', 'C301.1', 'C302.1', 'C303.1', 'R301.1'},
    '/GND': {'U301.1', 'U301.4', 'U301.5', 'U301.9', 'C301.2', 'C302.2', 'C303.2', 'C304.2'},
    '/TFT_PWR_EN': {'U301.3', 'R301.2'},
    '/TFT_PWR_SW': {'U301.7', 'L301.1'},
    '/TFT_3V3': {'U301.6', 'L301.2', 'C304.1', 'R302.1'},
    '/TFT_3V3_PG': {'U301.8', 'R302.2'},
}
root = ET.parse(HERE / (NAME + '.xml')).getroot()
nets = root.findall('./nets/net')
actual = {n.attrib['name']: {x.attrib['ref'] + '.' + x.attrib['pin'] for x in n.findall('node')} for n in nets}
assert len(nets) == 6 and actual == expected
assert sum(len(n.findall('node')) for n in nets) == 23
saved_map = json.loads((HERE / 'NET_MAP.json').read_text())
assert {name: set(nodes) for name, nodes in saved_map['nets'].items()} == expected
values = {
    'U301': 'TPS62162DSGR', 'L301': '744025003 3.3uH',
    'C301': '10uF GRM21BR61E106KA73L', 'C302': '10uF GRM21BR61E106KA73L',
    'C303': '100nF GRM188R71E104KA01D', 'C304': '47uF GRM32ER61C476KE15L',
    'R301': '100k 1% EN', 'R302': '100k 1% PG',
}
components = root.findall('./components/comp')
assert len(components) == 8
assert {c.attrib['ref']: c.findtext('value') for c in components} == values
for component in components:
    if component.attrib['ref'] in ('R301', 'R302'):
        fields = {f.attrib['name']: f.text for f in component.findall('./fields/field')}
        assert fields['MPN'] == 'RC0603FR-07100KL'
        assert fields['Manufacturer'] == 'Yageo'
        assert fields['Supplier'] == 'ChipDip9000632023'
        assert fields['Supplier_URL'] == 'https://www.chipdip.ru/product/rc0603fr-07100kl'
        assert component.findtext('datasheet') == 'https://static.chipdip.ru/lib/720/DOC012720002.pdf'
    footprint = component.find('./fields/field[@name="Footprint"]')
    assert footprint is not None and footprint.text is None
part = root.find('./libparts/libpart[@part="TPS62170DSG"]')
assert part is not None
assert {p.attrib['num']: p.attrib['name'] for p in part.findall('./pins/pin')} == {
    '1': 'PGND', '2': 'VIN', '3': 'EN', '4': 'AGND', '5': 'FB',
    '6': 'VOS', '7': 'SW', '8': 'PG', '9': 'PAD',
}
sch = (HERE / (NAME + '.kicad_sch')).read_text()
refs = re.findall(r'\(property "Reference" "([^"]+)"', sch)
assert set(refs) == set(values) | {'U', 'L', 'C', 'R', '#FLG', '#FLG301', '#FLG302'}
flags = json.loads((HERE / 'PWR_FLAG_READBACK.json').read_text())['power_flags']
assert [(f['reference'], f['pin'], f['net']) for f in flags] == [('#FLG301', '1', '+5V_CARRIER'), ('#FLG302', '1', 'GND')]
assert 'DRAFT / NOT FOR ORDER / NO FIRE' in sch
png = (HERE / (NAME + '.png')).read_bytes()
assert png[:8] == b'\x89PNG\r\n\x1a\n' and struct.unpack('>II', png[16:24]) == (2105, 1489)
erc = json.loads((HERE / (NAME + '-erc.json')).read_text())
assert erc['kicad_version'] == '10.0.7'
assert sum(len(s['violations']) for s in erc['sheets']) == 0
assert {v['key'] for v in erc['ignored_checks']} == {'single_global_label', 'four_way_junction', 'simulation_model_issue', 'footprint_filter'}
print('PASS: hashes, exact files, 6 nets / 23 pins, 8 values, 9 U301 pins, empty footprints, 2 recorded PWR_FLAG, PNG, ERC (4 ignored checks). DRAFT; standalone_not_integrated; NO FIRE.')
