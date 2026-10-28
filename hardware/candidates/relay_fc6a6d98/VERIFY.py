#!/usr/bin/env python3
import csv
import hashlib
import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent
PCB = "fc6a6d98efadc539fd21af9af2b8381dd91618b5b57a4ce21c1aadf7d15004e3"
ROOT_SCH = "49a81ce6383e121ec857c6cdf1ce1889b7179d3dfc686141b4a27563874bbfbe"


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


for line in (ROOT / "SHA256SUMS").read_text().splitlines():
    digest, relative = line.split("  ", 1)
    assert sha256(ROOT / relative) == digest, relative
assert sha256(ROOT / "project/Sirokko26201_control_motor_candidate.kicad_pcb") == PCB
assert sha256(ROOT / "project/Sirokko26201_control_motor_candidate.kicad_sch") == ROOT_SCH

root_sch = (ROOT / "project/Sirokko26201_control_motor_candidate.kicad_sch").read_text()
children = re.findall(r'\(property "Sheetfile" "([^"]+)"', root_sch)
assert len(children) == 5 and all((ROOT / "project" / child).is_file() for child in children), children
assert len(list((ROOT / "project").rglob("*.kicad_sch"))) == 6

status = json.loads((ROOT / "PUBLIC_STATUS.json").read_text())
assert status["production_release"] is False
assert status["project_files"] == sum(p.is_file() for p in (ROOT / "project").rglob("*")) == 77
assert status["physical_connectivity"]["pads"] == [533, 582]
assert status["physical_connectivity"]["pairs"] == [5646, 5648]
assert status["physical_connectivity"]["lost_pairs"] == 0
assert status["physical_connectivity"]["added_pairs"] == 2
assert set(status["physical_connectivity"]["new_pair_labels"]) == {"C23.1-U12.14", "C23.2-U12.7"}
assert status["physical_connectivity"]["cross_net_pairs"] == [0, 0]
assert status["erc"] == {"sheets": 6, "errors": 3, "warnings": 6}
assert status["drc"]["handed_summary"] == {"violations": 27, "unconnected_items": 150, "schematic_parity": 14}
assert status["drc"]["archived_final_json"] == {"violations": 27, "unconnected_items": 164, "schematic_parity": 11}

with (ROOT / "verification/FINAL_BOM_DETAILED.csv").open(newline="", encoding="utf-8-sig") as handle:
    rows = list(csv.DictReader(handle))
assert len(rows) == 140
assert sum(bool(row["MPN"].strip()) for row in rows) == 121

for manifest_dir in (ROOT / "project/tft_power_tps62162_standalone_DRAFT", ROOT / "project/tft_power_tps62162_standalone_DRAFT/passives_v3"):
    for line in (manifest_dir / "SHA256SUMS").read_text().splitlines():
        digest, relative = line.split("  ", 1)
        assert sha256(manifest_dir / relative) == digest, manifest_dir / relative

text_suffixes = {".md", ".json", ".xml", ".py", ".csv", ".txt"}
private_ip = re.compile(r"(?:^|[^0-9])(?:10(?:\.[0-9]{1,3}){3}|192\.168(?:\.[0-9]{1,3}){2}|172\.(?:1[6-9]|2[0-9]|3[01])(?:\.[0-9]{1,3}){2})(?:$|[^0-9])")
secret = re.compile(r"ghp_[A-Za-z0-9]{20,}|github_pat_[A-Za-z0-9_]{20,}|BEGIN [A-Z ]*PRIVATE KEY")
for path in ROOT.rglob("*"):
    if not path.is_file() or path.suffix.lower() not in text_suffixes or path == ROOT / "VERIFY.py":
        continue
    text = path.read_text(errors="ignore")
    blocked_paths = ["/" + name + "/" for name in ("root", "home", "projects")]
    assert not any(value in text for value in blocked_paths), path
    assert not private_ip.search(text), path
    assert not secret.search(text), path

assert not (ROOT / "verification/verify_connectivity_delta.py").exists()
assert not (ROOT / "verification/CONNECTIVITY_DELTA.json").exists()
print(json.dumps({"status": "PASS", "project_files": 77, "sheets": 6, "bom": [140, 121, 19], "physical_pairs": [5646, 5648]}, ensure_ascii=False))
