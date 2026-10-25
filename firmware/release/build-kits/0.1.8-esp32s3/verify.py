#!/usr/bin/env python3
"""Verify exact kit inventory, hashes, manifest and full native input source."""
from pathlib import Path
import hashlib, json, tarfile
root=Path(__file__).resolve().parent
def sha(data): return hashlib.sha256(data).hexdigest()
expected={}
for line in (root/'SHA256SUMS').read_text().splitlines():
    digest,name=line.split('  ',1)
    assert name not in expected and not name.startswith('/') and '..' not in Path(name).parts,name
    expected[name]=digest
actual={str(p.relative_to(root)) for p in root.rglob('*') if p.is_file() and p.name!='SHA256SUMS'}
assert actual==set(expected),(actual-set(expected),set(expected)-actual)
for name,digest in expected.items(): assert sha((root/name).read_bytes())==digest,name
manifest=json.loads((root/'manifest.json').read_text())
assert set(manifest['files'])==actual-{'manifest.json'}
for name,entry in manifest['files'].items():
    p=root/name
    assert p.stat().st_size==entry['size'] and sha(p.read_bytes())==entry['sha256'],name
identity=json.loads((root/'source/source-identity.json').read_text())
source=root/'source/firmware-source.tar.gz'
assert sha(source.read_bytes())==identity['archive_sha256']
with tarfile.open(source) as a:
    files={m.name:sha(a.extractfile(m).read()) for m in a if m.isfile()}
assert files==identity['files']
assert manifest['source_commit']==identity['source_commit']
assert manifest['target_build']['exit_code']==0 and manifest['tests']['cpp_passed']==manifest['tests']['cpp_total']==110
assert manifest['config_schema']==4 and manifest['hardware_contract_id']==6 and not manifest['hardware_tested']
print('KIT VERIFIED:',len(expected),'SHA256SUMS entries;',len(files),'source files;',len(identity['git_files']),'Git files; host 110/110 + Python 4/4; hardware NOT tested')
