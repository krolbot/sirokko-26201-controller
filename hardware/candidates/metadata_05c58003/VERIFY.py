#!/usr/bin/env python3
import csv
import hashlib
import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent
EXPECTED = {
    "project/Sirokko26201_control_motor_candidate.kicad_pcb": "05c58003722be967ed31e54bef893a04c161347910efcb29c9e27a26fb0b0ab2",
    "project/Sirokko26201_control_motor_candidate.kicad_sch": "3fa9d05afd6750a27b56ec9dd9dad9b9e80d0631f0fe072cb2c67f2f5674aa50",
    "project/tft_reset_local_DRAFT/sirokko_tft_reset_local_DRAFT.kicad_sch": "5440a7318222fc539a87b6a25773cc005a77c1ec5f0411951747006176d8b939",
}


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


for line in (ROOT / "SHA256SUMS").read_text().splitlines():
    digest, relative = line.split("  ", 1)
    assert sha256(ROOT / relative) == digest, relative

assert len([p for p in (ROOT / "project").rglob("*") if p.is_file()]) == 76
assert len(list((ROOT / "project").rglob("*.kicad_sch"))) == 6
for relative, digest in EXPECTED.items():
    assert sha256(ROOT / relative) == digest, relative

with (ROOT / "verification/production_bom.csv").open(newline="") as handle:
    rows = list(csv.DictReader(handle))
assert len(rows) == 128
assert sum(bool(row["MPN"].strip()) for row in rows) == 110

for path in ROOT.rglob("*.md"):
    text = path.read_text()
    assert "/root/" not in text and "/projects/" not in text, path
    assert not re.search(r"https?://(?:localhost|127\.0\.0\.1|10\.|192\.168\.|172\.(?:1[6-9]|2\d|3[01])\.)", text), path

print(json.dumps({"status": "PASS", "project_files": 76, "sheets": 6, "bom_rows": 128, "mpn_filled": 110}, ensure_ascii=False))
