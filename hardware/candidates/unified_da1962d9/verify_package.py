#!/usr/bin/env python3
import csv
import hashlib
import json
from pathlib import Path

root = Path(__file__).resolve().parent
failures = []
for line in (root / "SHA256SUMS").read_text().splitlines():
    expected, relative = line.split("  ", 1)
    path = root / relative
    actual = hashlib.sha256(path.read_bytes()).hexdigest() if path.is_file() else "MISSING"
    if actual != expected:
        failures.append(f"sha256 {relative}: {actual} != {expected}")

result = json.loads((root / "OPERATION_RESULT.json").read_text())
required_false = {
    "layout_accepted",
    "general_routing_allowed",
    "firmware_allowed",
    "production_release",
    "order_allowed",
    "power_on_or_fire_test_allowed",
}
for key in required_false:
    if result["acceptance"].get(key) is not False:
        failures.append(f"acceptance.{key} must be false")
if not result["final_candidate"]["physical_connectivity_equal_source"]:
    failures.append("physical connectivity equality is false")
if result["checks"]["drc"]["passed"] or result["checks"]["erc"]["passed"]:
    failures.append("DRC/ERC must remain explicitly not passed")

with (root / "reports/BOM_EXACT_MPN.csv").open(newline="") as stream:
    bom = list(csv.DictReader(stream))
missing_mpn = [row["Refs"] for row in bom if not row["MPN"].strip()]
if len(bom) != 159 or missing_mpn:
    failures.append(f"BOM rows={len(bom)}, missing MPN={missing_mpn}")

if failures:
    raise SystemExit("PACKAGE VERIFICATION FAILED\n" + "\n".join(failures))
print("PACKAGE VERIFICATION OK")
print("159 BOM rows have MPN; physical connectivity unchanged; all release/ordering flags remain false.")
