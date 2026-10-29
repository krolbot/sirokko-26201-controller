#!/usr/bin/env python3
"""Compare complete native KiCad pad groups without allowing old groups to split."""

from __future__ import annotations

import argparse
import json
from pathlib import Path


def load(path: Path) -> dict[str, object]:
    return json.loads(path.read_text(encoding="utf-8"))


def index(payload: dict[str, object]) -> tuple[dict[str, dict[str, object]], dict[str, frozenset[str]], dict[str, list[str]]]:
    pads: dict[str, dict[str, object]] = {}
    membership: dict[str, frozenset[str]] = {}
    labels: dict[str, list[str]] = {}
    for group in payload["groups"]:  # type: ignore[index]
        uuids = frozenset(str(pad["uuid"]) for pad in group["pads"])
        for pad in group["pads"]:
            uuid = str(pad["uuid"])
            pads[uuid] = pad
            membership[uuid] = uuids
            labels.setdefault(f"{pad['reference']}.{pad['pad']}", []).append(uuid)
    return pads, membership, labels


def check_preservation(before: dict[str, object], after: dict[str, object]) -> dict[str, object]:
    before_pads, before_groups, _ = index(before)
    after_pads, after_groups, _ = index(after)
    missing = sorted(set(before_pads) - set(after_pads))
    added = sorted(set(after_pads) - set(before_pads))
    unique_before = {group for group in before_groups.values()}
    split = []
    expanded = []
    unchanged = 0
    for group in sorted(unique_before, key=lambda item: tuple(sorted(item))):
        destinations = {after_groups[uuid] for uuid in group if uuid in after_groups}
        if len(destinations) != 1 or not group.issubset(next(iter(destinations), frozenset())):
            split.append({"before": sorted(group), "after": [sorted(item) for item in destinations]})
            continue
        destination = next(iter(destinations))
        if destination == group:
            unchanged += 1
        else:
            expanded.append({"before": sorted(group), "after": sorted(destination)})
    return {
        "before_pad_count": len(before_pads),
        "after_pad_count": len(after_pads),
        "before_group_count": len(unique_before),
        "after_group_count": len({group for group in after_groups.values()}),
        "missing_pad_uuids": missing,
        "added_pad_uuids": added,
        "unchanged_old_groups": unchanged,
        "expanded_old_groups": expanded,
        "split_old_groups": split,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("before", type=Path)
    parser.add_argument("after", type=Path)
    parser.add_argument("--target", action="append", default=[], metavar="NAME=REF.PAD,REF.PAD")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    before = load(args.before)
    after = load(args.after)
    result = check_preservation(before, after)
    after_pads, after_groups, labels = index(after)
    targets = []
    failures = []
    for spec in args.target:
        name, raw_labels = spec.split("=", 1)
        requested = raw_labels.split(",")
        selected: list[str] = []
        missing_labels = []
        ambiguous_labels = []
        for label in requested:
            found = labels.get(label, [])
            if not found:
                missing_labels.append(label)
            elif len(found) != 1:
                ambiguous_labels.append({"label": label, "uuids": found})
            else:
                selected.append(found[0])
        groups = {after_groups[uuid] for uuid in selected}
        connected = not missing_labels and not ambiguous_labels and len(groups) == 1
        group_rows = []
        if connected:
            group_rows = [after_pads[uuid] for uuid in sorted(next(iter(groups)))]
        row = {
            "name": name,
            "requested": requested,
            "missing_labels": missing_labels,
            "ambiguous_labels": ambiguous_labels,
            "connected": connected,
            "after_group": group_rows,
        }
        targets.append(row)
        if not connected:
            failures.append(f"target not connected: {name}")

    if result["missing_pad_uuids"]:
        failures.append("old pad UUIDs missing")
    if result["added_pad_uuids"]:
        failures.append("unexpected new pad UUIDs")
    if result["split_old_groups"]:
        failures.append("old native pad groups split")

    # Negative test: split a non-singleton old group and require detection.
    before_copy = json.loads(json.dumps(before))
    group = next(group for group in before_copy["groups"] if len(group["pads"]) > 1)
    removed = group["pads"].pop()
    before_copy["groups"].append({"group_id": removed["uuid"], "nets": [removed["net"]], "pads": [removed]})
    negative_detected = bool(check_preservation(before, before_copy)["split_old_groups"])
    if not negative_detected:
        failures.append("negative split mutation was not detected")

    payload = {
        "status": "PASS" if not failures else "FAIL",
        "before": str(args.before),
        "after": str(args.after),
        "preservation": result,
        "targets": targets,
        "negative_group_split_detected": negative_detected,
        "failures": failures,
    }
    args.output.write_text(json.dumps(payload, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    expanded_count = len(result["expanded_old_groups"]) if isinstance(result["expanded_old_groups"], list) else 0
    print(json.dumps({"status": payload["status"], "old_groups": result["before_group_count"], "new_groups": result["after_group_count"], "expanded": expanded_count, "targets": {row["name"]: row["connected"] for row in targets}}, ensure_ascii=False))
    return 0 if not failures else 1


if __name__ == "__main__":
    raise SystemExit(main())
