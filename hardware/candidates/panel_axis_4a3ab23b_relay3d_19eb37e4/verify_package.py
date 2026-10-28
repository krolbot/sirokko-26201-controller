#!/usr/bin/env python3
import hashlib
import json
import re
from collections import Counter
from pathlib import Path

root = Path(__file__).resolve().parent
failures = []


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


actual_files = {
    path.relative_to(root).as_posix()
    for path in root.rglob("*")
    if path.is_file()
}

checksum_entries = {}
for line in (root / "SHA256SUMS").read_text().splitlines():
    expected, relative = line.split("  ", 1)
    if relative in checksum_entries:
        failures.append(f"duplicate checksum path: {relative}")
        continue
    checksum_entries[relative] = expected
    path = root / relative
    actual = sha256(path) if path.is_file() else "MISSING"
    if actual != expected:
        failures.append(f"sha256 {relative}: {actual} != {expected}")

expected_checksum_paths = actual_files - {"SHA256SUMS"}
if set(checksum_entries) != expected_checksum_paths:
    failures.append(
        "SHA256SUMS path set mismatch: "
        f"missing={sorted(expected_checksum_paths - set(checksum_entries))}, "
        f"extra={sorted(set(checksum_entries) - expected_checksum_paths)}"
    )

manifest = json.loads((root / "PACKAGE_MANIFEST.json").read_text())
if len(actual_files) != manifest["publication"]["total_files_including_manifest_filelist_sha256sums_and_verifier"]:
    failures.append(f"package file count: {len(actual_files)}")

required_false = {
    "layout_accepted",
    "full_layout_accepted",
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
    "project/Sirokko26201_control_motor_candidate.kicad_pcb": "4a3ab23b0998b5e5730ba60325a51cf142fe0b877d3b37196c24bca61bb79a9b",
    "project/Sirokko26201_control_motor_candidate.kicad_sch": "abec756a479dc1fa352ee6db5f0ed0c6f03ea472992acd5553bc54deffdde868",
    "project/Sirokko26201_control_motor_candidate.kicad_pro": "54cf96572352ec0b91e7aaa03886a3f18396c1eaf5db8b1e98d5041825b5b964",
    "project/3dmodels/DG85BM_DZ85AB_installed_envelope.wrl": "19eb37e4deffac96e27576c6fe0d4ebe48d0eab7de7a91b7af54c989769f5d8d",
    "reports/FULL_NETLIST.net": "bff2361da87415342028b542c6aa82a84dea98ded68e4f0eed2ad99b0ea4af6f",
}.items():
    path = root / relative
    actual = sha256(path) if path.is_file() else "MISSING"
    if actual != expected:
        failures.append(f"frozen/public sha256 {relative}: {actual} != {expected}")

project = root / "project"
counts = Counter(path.suffix for path in project.rglob("*") if path.is_file())
expected_counts = {
    ".kicad_pcb": 3,
    ".kicad_pro": 4,
    ".kicad_sch": 7,
    ".kicad_sym": 9,
    ".kicad_mod": 30,
    ".wrl": 7,
}
for suffix, expected in expected_counts.items():
    if counts[suffix] != expected:
        failures.append(f"project count {suffix}: {counts[suffix]} != {expected}")

schematic = (project / "Sirokko26201_control_motor_candidate.kicad_sch").read_text()
sheetfiles = re.findall(r'\(property "Sheetfile" "([^"]+)"', schematic)
if len(sheetfiles) != 6:
    failures.append(f"root Sheetfile count: {len(sheetfiles)} != 6")
for relative in sheetfiles:
    if not (project / relative).is_file():
        failures.append(f"missing hierarchical sheet: {relative}")

library_targets = []
for table in ("fp-lib-table", "sym-lib-table"):
    text = (project / table).read_text()
    library_targets.extend(re.findall(r'\$\{KIPRJMOD\}/([^"\)]+)', text))
if len(library_targets) != 15:
    failures.append(f"root library target count: {len(library_targets)} != 15")
for relative in library_targets:
    if not (project / relative).exists():
        failures.append(f"missing local dependency: {relative}")

historical_pcbs = {
    "baseline_before_18366.kicad_pcb",
    "before_safezone_reroute.kicad_pcb",
}
if set(manifest["project"]["historical_pcb_files"]) != historical_pcbs:
    failures.append("historical PCB manifest does not match the two preserved baselines")

netlist = (root / "reports/FULL_NETLIST.net").read_text()
if not netlist.lstrip().startswith("(export"):
    failures.append("FULL_NETLIST.net is not a KiCad S-expression export")
if 'source "project/Sirokko26201_control_motor_candidate.kicad_sch"' not in netlist:
    failures.append("FULL_NETLIST.net does not use the public relative source path")

for nested in (root / "evidence/external_wiring/SHA256SUMS",):
    for line in nested.read_text().splitlines():
        expected, relative = line.split("  ", 1)
        path = nested.parent / relative
        actual = sha256(path) if path.is_file() else "MISSING"
        if actual != expected:
            failures.append(f"external wiring sha256 {relative}: {actual} != {expected}")

for forbidden_path in (
    root / "tools",
    project / "3dmodels/Waveshare_18366_k01-2_official_nominal.stp",
):
    if forbidden_path.exists():
        failures.append(f"forbidden published path: {forbidden_path.relative_to(root)}")

for path in root.rglob("*"):
    if not path.is_file():
        continue
    if path.suffix.lower() in {".stp", ".step", ".pdf"}:
        failures.append(f"forbidden third-party-capable format: {path.relative_to(root)}")
    try:
        text = path.read_text()
    except UnicodeDecodeError:
        continue
    for marker in (
        "/" + "root/",
        "/" + "projects/",
        "." + "hermes",
        "gh" + "p_",
        "github_" + "pat_",
        "BEGIN OPENSSH " + "PRIVATE KEY",
    ):
        if marker in text:
            failures.append(f"private/secret marker {marker!r}: {path.relative_to(root)}")

if failures:
    raise SystemExit("PACKAGE VERIFICATION FAILED\n" + "\n".join(failures))

print("PACKAGE VERIFICATION OK")
print(f"files={len(actual_files)} native_project_files=66 sheets=7/7 root_library_targets=15/15")
print("PCB/SCH/PRO/WRL and public netlist hashes match; DRAFT release/order/power/fire flags remain false.")
