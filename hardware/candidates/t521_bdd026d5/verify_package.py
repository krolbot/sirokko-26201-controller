#!/usr/bin/env python3
import hashlib
import json
import re
from pathlib import Path

root = Path(__file__).resolve().parent
failures = []

for line in (root / "SHA256SUMS").read_text().splitlines():
    expected, relative = line.split("  ", 1)
    path = root / relative
    actual = hashlib.sha256(path.read_bytes()).hexdigest() if path.is_file() else "MISSING"
    if actual != expected:
        failures.append(f"sha256 {relative}: {actual} != {expected}")

manifest = json.loads((root / "PACKAGE_MANIFEST.json").read_text())
required_false = {
    "layout_accepted",
    "general_routing_allowed",
    "firmware_allowed",
    "production_release",
    "order_allowed",
    "power_on_or_fire_test_allowed",
}
for key in required_false:
    if manifest["acceptance"].get(key) is not False:
        failures.append(f"acceptance.{key} must be false")

for relative, expected in {
    "project/Sirokko26201_control_motor_candidate.kicad_pcb": "bdd026d53029a05542647896575e8c83abd9b3d20d2b4bf2bddf866e07925f06",
    "project/Sirokko26201_control_motor_candidate.kicad_sch": "abec756a479dc1fa352ee6db5f0ed0c6f03ea472992acd5553bc54deffdde868",
}.items():
    actual = hashlib.sha256((root / relative).read_bytes()).hexdigest()
    if actual != expected:
        failures.append(f"frozen native sha256 {relative}: {actual} != {expected}")

project = root / "project"
schematic = (project / "Sirokko26201_control_motor_candidate.kicad_sch").read_text()
for relative in re.findall(r'\(property "Sheetfile" "([^"]+)"', schematic):
    if not (project / relative).is_file():
        failures.append(f"missing hierarchical sheet: {relative}")
for table in ("fp-lib-table", "sym-lib-table"):
    text = (project / table).read_text()
    for relative in re.findall(r'\$\{KIPRJMOD\}/([^"\)]+)', text):
        if not (project / relative).exists():
            failures.append(f"missing local dependency from {table}: {relative}")

for forbidden in ("/" + "root/", "/" + "projects/", "." + "hermes"):
    for path in root.rglob("*"):
        if not path.is_file():
            continue
        try:
            text = path.read_text()
        except UnicodeDecodeError:
            continue
        if forbidden in text:
            failures.append(f"private path marker {forbidden!r}: {path.relative_to(root)}")

if (root / "tools").exists():
    failures.append("source tools directory must not be published")
if (project / "3dmodels/Waveshare_18366_k01-2_official_nominal.stp").exists():
    failures.append("unlicensed Waveshare STEP must not be published")

if failures:
    raise SystemExit("PACKAGE VERIFICATION FAILED\n" + "\n".join(failures))
print("PACKAGE VERIFICATION OK")
print("Frozen PCB/root SCH match; hierarchy and local libraries are complete; release, ordering, power and fire-test flags remain false.")
