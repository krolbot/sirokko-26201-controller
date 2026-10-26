#!/usr/bin/env python3
"""Verify this BENCH ONLY kit with stdlib; never connect or flash hardware."""
from pathlib import Path, PurePosixPath
import hashlib, json, struct, tarfile, zipfile, zlib

ROOT = Path(__file__).resolve().parent

def safe_name(name):
    path = PurePosixPath(name)
    assert not path.is_absolute() and '..' not in path.parts, name
    return name

def main():
    expected = {}
    for line in (ROOT/'SHA256SUMS').read_text().splitlines():
        digest, name = line.split('  ', 1)
        safe_name(name)
        assert name not in expected
        expected[name] = digest
    zip_name = 'Sirokko_0.1.9-DRAFT_BENCH_ONLY.zip'
    actual = {str(p.relative_to(ROOT)) for p in ROOT.rglob('*') if p.is_file() and str(p.relative_to(ROOT)) not in ('SHA256SUMS', zip_name)}
    assert set(expected) == actual, (set(expected)-actual, actual-set(expected))
    for name, digest in expected.items():
        assert hashlib.sha256((ROOT/name).read_bytes()).hexdigest() == digest, name
    if (ROOT/zip_name).exists():
        with zipfile.ZipFile(ROOT/zip_name) as archive:
            names = set(expected) | {'SHA256SUMS'}
            prefix = 'sirokko-0.1.9-bench/'
            assert archive.testzip() is None
            assert len(archive.namelist()) == len(names)
            assert set(archive.namelist()) == {prefix+n for n in names}
            for name in names:
                assert archive.read(prefix+name) == (ROOT/name).read_bytes(), name
    manifest = json.loads((ROOT/'manifest.json').read_text())
    identity = json.loads((ROOT/'source/source-identity.json').read_text())
    git = json.loads((ROOT/'source/source-git-identity.json').read_text())
    assert identity['source_commit'] == git['source_commit'] == manifest['source_commit']
    with tarfile.open(ROOT/'source/firmware-source.tar.gz') as archive:
        entries = archive.getmembers()
        assert all(e.isfile() and safe_name(e.name) for e in entries)
        assert len(entries) == len({e.name for e in entries}) == len(identity['files'])
        assert {e.name for e in entries} == {r['path'] for r in identity['files']}
        source = {e.name: archive.extractfile(e).read() for e in entries}
        for record in identity['files']:
            data = source[record['path']]
            assert len(data) == record['size'] and hashlib.sha256(data).hexdigest() == record['sha256'], record['path']
        for record in git['files']:
            assert hashlib.sha256(source[record['path']]).hexdigest() == record['sha256'], record['path']
    remote = json.loads((ROOT/'logs/exact-source-tree-readback.json').read_text())
    assert remote['exact_commit'] == manifest['source_commit'] and remote['verified'] == len(git['files'])
    for record in remote['files']:
        data = source[record['path']]
        assert hashlib.sha1(b'blob '+str(len(data)).encode()+b'\0'+data).hexdigest() == record['git_blob_sha']
    flash = json.loads((ROOT/'firmware/flasher_args.json').read_text())
    expected_offsets = {'0x0','0x8000','0x10000','0x20000'}
    assert set(flash['flash_files']) == expected_offsets and flash['flash_settings']['flash_size'] == '8MB'
    for offset, name in flash['flash_files'].items():
        data = (ROOT/'firmware'/safe_name(name)).read_bytes()
        record = manifest['flash_files'][offset]
        assert len(data) == record['size'] and hashlib.sha256(data).hexdigest() == record['sha256']
    partitions = []
    data = (ROOT/'firmware/partition_table/partition-table.bin').read_bytes()
    for pos in range(0, len(data), 32):
        magic, kind, subkind, offset, size, label, flags = struct.unpack('<HBBII16sI', data[pos:pos+32])
        if magic == 0xffff: break
        if magic == 0xebeb:
            assert hashlib.md5(data[:pos]).digest() == data[pos+16:pos+32]
            continue
        assert magic == 0x50aa
        partitions.append((label.rstrip(b'\0').decode(), kind, subkind, offset, size))
    app_size = (ROOT/'firmware/sirokko_26201_controller.bin').stat().st_size
    slots = [p for p in partitions if p[0] in ('ota_0','ota_1')]
    assert len(slots) == 2 and [p[3] for p in slots] == [0x20000,0x320000]
    assert all(p[4] == 0x300000 and app_size <= p[4] for p in slots)
    config = json.loads((ROOT/'build-metadata/config/sdkconfig.json').read_text())
    for key,value in {'IDF_TARGET':'esp32s3','ESPTOOLPY_FLASHSIZE':'8MB','SPIRAM_MODE_OCT':True,
                      'SPIRAM_SPEED_80M':True,'BOOTLOADER_APP_ROLLBACK_ENABLE':True}.items():
        assert config[key] == value, key
    images = sorted((ROOT/'ui').glob('*.png'))
    assert len(images) == manifest['ui']['png_files']
    for image in images:
        data=image.read_bytes(); assert data[:8] == b'\x89PNG\r\n\x1a\n'; pos=8
        while pos < len(data):
            size=struct.unpack('>I',data[pos:pos+4])[0]; block=data[pos+4:pos+8+size]
            assert zlib.crc32(block) == struct.unpack('>I',data[pos+8+size:pos+12+size])[0], image
            pos += size+12
        assert pos == len(data)
    print(json.dumps({'status':'VERIFIED BENCH ONLY — NO HARDWARE QUALIFICATION', 'kit_files':len(expected),
                      'source_files':len(identity['files']),'git_source_files':len(git['files']),
                      'source_commit':manifest['source_commit'],'app_bytes':app_size,
                      'free_bytes_each_ota':0x300000-app_size,'png_files':len(images)},ensure_ascii=False))

if __name__ == '__main__':
    main()
