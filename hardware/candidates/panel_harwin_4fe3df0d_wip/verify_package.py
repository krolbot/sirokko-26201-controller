#!/usr/bin/env python3
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parent


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


manifest = json.loads((ROOT / "PUBLIC_MANIFEST.json").read_text(encoding="utf-8"))
for item in manifest["published_files"]:
    path = (ROOT / item["public_path"]).resolve()
    assert path.is_file(), item["public_path"]
    assert sha256(path) == item["public_sha256"], item["public_path"]

for line in (ROOT / "SHA256SUMS").read_text(encoding="utf-8").splitlines():
    expected, relative = line.split("  ", 1)
    path = ROOT / relative
    assert path.is_file(), relative
    assert sha256(path) == expected, relative

project = ROOT / "project"
assert len(list(project.rglob("*.kicad_sch"))) == 7
assert len(list(project.rglob("*.kicad_pro"))) == 4
assert len(list(project.rglob("*.kicad_pcb"))) == 1
assert len(list(project.rglob("*.kicad_sym"))) == 9
assert len(list(project.rglob("*.kicad_mod"))) == 29
assert len(list(project.rglob("*.wrl"))) == 8
assert not list(project.rglob("*.stp"))
assert not list(project.rglob("*.step"))
assert not list(project.rglob("*.pdf"))
assert sha256(project / "Sirokko26201_control_motor_candidate.kicad_pcb") == "4fe3df0d78b568ea887debc1af283c9adb6c64013222b72591d7b5f04e4a3772"
print(f"OK: {len(manifest['published_files'])} published files verified")
