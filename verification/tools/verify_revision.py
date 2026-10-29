#!/usr/bin/env python3
"""Minimal reproducible regression checks for Sirokko KiCad revisions."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import tempfile
from pathlib import Path


def balanced_blocks(text: str, name: str) -> list[str]:
    starts = [m.start() for m in re.finditer(rf"(?m)^\s*\({re.escape(name)}\b", text)]
    blocks: list[str] = []
    for start in starts:
        depth = 0
        quoted = escaped = False
        for index in range(start, len(text)):
            char = text[index]
            if quoted:
                if escaped:
                    escaped = False
                elif char == "\\":
                    escaped = True
                elif char == '"':
                    quoted = False
                continue
            if char == '"':
                quoted = True
            elif char == "(":
                depth += 1
            elif char == ")":
                depth -= 1
                if depth == 0:
                    blocks.append(text[start : index + 1])
                    break
        else:
            raise ValueError(f"unterminated ({name} block at byte {start}")
    return blocks


def one(pattern: str, block: str, field: str) -> str:
    match = re.search(pattern, block)
    if not match:
        raise ValueError(f"missing {field} in {block[:120]!r}")
    return " ".join(match.groups())


def copper(path: Path) -> dict[str, dict[str, str]]:
    text = path.read_text(encoding="utf-8")
    result: dict[str, dict[str, str]] = {}
    for kind in ("segment", "via"):
        for block in balanced_blocks(text, kind):
            uuid = one(r'\(uuid\s+"([^\"]+)"\)', block, "uuid")
            if kind == "segment":
                fields = {
                    "type": kind,
                    "start": one(r"\(start\s+([^\s()]+)\s+([^\s()]+)\)", block, "start"),
                    "end": one(r"\(end\s+([^\s()]+)\s+([^\s()]+)\)", block, "end"),
                    "width": one(r"\(width\s+([^\s()]+)\)", block, "width"),
                    "layers": one(r'\(layer\s+"([^\"]+)"\)', block, "layer"),
                    "net": one(r"\(net\s+([^\s()]+)\)", block, "net"),
                }
            else:
                fields = {
                    "type": kind,
                    "at": one(r"\(at\s+([^\s()]+)\s+([^\s()]+)\)", block, "at"),
                    "size": one(r"\(size\s+([^\s()]+)\)", block, "size"),
                    "drill": one(r"\(drill\s+([^\s()]+)\)", block, "drill"),
                    "layers": one(r'\(layers\s+"([^\"]+)"\s+"([^\"]+)"\)', block, "layers"),
                    "net": one(r"\(net\s+([^\s()]+)\)", block, "net"),
                }
            if uuid in result:
                raise ValueError(f"duplicate copper UUID {uuid}")
            result[uuid] = fields
    return result


def compare(before: dict[str, dict[str, str]], after: dict[str, dict[str, str]]) -> dict[str, object]:
    before_ids, after_ids = set(before), set(after)
    shared = before_ids & after_ids
    changed = {uuid: {"before": before[uuid], "after": after[uuid]} for uuid in shared if before[uuid] != after[uuid]}
    return {
        "before_count": len(before),
        "after_count": len(after),
        "preserved_count": len(shared),
        "added": {uuid: after[uuid] for uuid in sorted(after_ids - before_ids)},
        "removed": {uuid: before[uuid] for uuid in sorted(before_ids - after_ids)},
        "changed": changed,
    }


def local_references(project: Path) -> dict[str, object]:
    missing: list[dict[str, str]] = []
    checked: list[dict[str, str]] = []
    for path in project.rglob("*"):
        if not path.is_file() or path.suffix not in {".kicad_pcb", ".kicad_sch", ".kicad_mod", ".kicad_sym"}:
            continue
        text = path.read_text(encoding="utf-8", errors="replace")
        refs = re.findall(r'\(model\s+"([^\"]+)"', text)
        if path.suffix == ".kicad_sch":
            refs += re.findall(r'\(property\s+"Sheetfile"\s+"([^\"]+)"', text)
        for ref in refs:
            if ref.startswith("${KIPRJMOD}/"):
                target = project / ref.removeprefix("${KIPRJMOD}/")
            elif path.suffix == ".kicad_sch" and not ref.startswith("${"):
                target = path.parent / ref
            else:
                continue
            row = {"source": str(path.relative_to(project)), "reference": ref}
            checked.append(row)
            if not target.exists():
                missing.append(row)
    return {"checked_count": len(checked), "missing": missing}


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("before", type=Path)
    parser.add_argument("after", type=Path)
    parser.add_argument("--project", type=Path, required=True)
    parser.add_argument("--expected-added", type=int, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    before, after = copper(args.before), copper(args.after)
    delta = compare(before, after)
    refs = local_references(args.project)
    forbidden = [
        "TFT_M2x12_mounting_hardware_envelope.wrl",
        "970120244",
        "1253131",
    ]
    forbidden_hits: dict[str, list[str]] = {}
    for path in args.project.rglob("*"):
        if path.is_file() and path.suffix != ".step":
            text = path.read_text(encoding="utf-8", errors="ignore")
            hits = [token for token in forbidden if token in text]
            if hits:
                forbidden_hits[str(path.relative_to(args.project))] = hits

    # Negative test: one geometry mutation must be detected as a changed UUID.
    first_uuid = next(iter(after))
    mutated = {uuid: fields.copy() for uuid, fields in after.items()}
    key = "start" if mutated[first_uuid]["type"] == "segment" else "at"
    mutated[first_uuid][key] += " MUTATION"
    negative_detected = bool(compare(after, mutated)["changed"])

    result = {
        "status": "PASS",
        "before": str(args.before),
        "after": str(args.after),
        "before_sha256": sha256(args.before),
        "after_sha256": sha256(args.after),
        "copper_delta": delta,
        "local_references": refs,
        "forbidden_hits": forbidden_hits,
        "negative_geometry_mutation_detected": negative_detected,
    }
    failures = []
    if delta["removed"]:
        failures.append("copper removed")
    if delta["changed"]:
        failures.append("preserved copper changed")
    if len(delta["added"]) != args.expected_added:
        failures.append(f"expected {args.expected_added} additions, got {len(delta['added'])}")
    if refs["missing"]:
        failures.append("missing local CAD references")
    if forbidden_hits:
        failures.append("revoked TFT mounting references remain")
    if not negative_detected:
        failures.append("negative mutation was not detected")
    if failures:
        result["status"] = "FAIL"
        result["failures"] = failures

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"status": result["status"], "failures": failures, "before": len(before), "after": len(after), "added": len(delta["added"])}, ensure_ascii=False))
    return 0 if not failures else 1


if __name__ == "__main__":
    raise SystemExit(main())
