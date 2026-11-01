#!/usr/bin/env python3
import hashlib
import json
import re
import sys
import tempfile
from itertools import combinations
from pathlib import Path

TARGET_REFS = {"U20", "Q20", "D20", "R120", "R121", "R122", "R123", "C28"}
TARGET_NETS = {"/P24_PRESENT", "/P24_BASE", "/P24_SENSE_N", "/P24_SERIES_MID", "/P24_ZENER_K"}


def uid(item):
    return item.m_Uuid.AsString()


def pos(v):
    return [round(pcbnew.ToMM(v.x), 6), round(pcbnew.ToMM(v.y), 6)]


def snapshot(board_path):
    global pcbnew
    import pcbnew

    board_path = Path(board_path)
    board = pcbnew.LoadBoard(str(board_path))
    board.BuildConnectivity()
    connectivity = board.GetConnectivity()

    footprints = {}
    pads = {}
    for footprint in board.GetFootprints():
        ref = footprint.GetReference()
        footprints[ref] = {
            "uuid": uid(footprint),
            "value": footprint.GetValue(),
            "position_mm": pos(footprint.GetPosition()),
            "orientation_deg": round(footprint.GetOrientationDegrees(), 6),
            "layer": board.GetLayerName(footprint.GetLayer()),
        }
        for pad in footprint.Pads():
            pad_uid = uid(pad)
            pads[pad_uid] = {
                "ref": ref,
                "number": str(pad.GetNumber()),
                "net": pad.GetNetname(),
                "position_mm": pos(pad.GetPosition()),
                "drill_mm": pos(pad.GetDrillSize()),
                "size_mm": pos(pad.GetSize()),
                "layers": sorted(board.GetLayerName(layer) for layer in pad.GetLayerSet().Seq()),
            }

    pairs = set()
    for pad_uid, pad_record in pads.items():
        pad = next(
            candidate
            for footprint in board.GetFootprints()
            if footprint.GetReference() == pad_record["ref"]
            for candidate in footprint.Pads()
            if uid(candidate) == pad_uid
        )
        connected_pad_uids = {
            uid(item)
            for item in connectivity.GetConnectedItems(pad)
            if isinstance(item, pcbnew.PAD)
        }
        connected_pad_uids.add(pad_uid)
        for pair in combinations(sorted(connected_pad_uids), 2):
            pairs.add(pair)

    copper = {}
    for item in board.GetTracks():
        record = {
            "kind": "via" if isinstance(item, pcbnew.PCB_VIA) else "segment",
            "net": item.GetNetname(),
            "layer": board.GetLayerName(item.GetLayer()),
        }
        if isinstance(item, pcbnew.PCB_VIA):
            record.update({
                "at_mm": pos(item.GetPosition()),
                "drill_mm": round(pcbnew.ToMM(item.GetDrillValue()), 6),
                "diameter_mm": round(pcbnew.ToMM(item.GetWidth(pcbnew.F_Cu)), 6),
            })
        else:
            record.update({
                "start_mm": pos(item.GetStart()),
                "end_mm": pos(item.GetEnd()),
                "width_mm": round(pcbnew.ToMM(item.GetWidth()), 6),
            })
        copper[uid(item)] = record

    zones = {}
    for zone in board.Zones():
        zones[uid(zone)] = {
            "net": zone.GetNetname(),
            "layer": board.GetLayerName(zone.GetLayer()),
            "rule_area": bool(zone.GetIsRuleArea()),
            "keepout_tracks": bool(zone.GetDoNotAllowTracks()),
            "keepout_vias": bool(zone.GetDoNotAllowVias()),
            "keepout_footprints": bool(zone.GetDoNotAllowFootprints()),
        }

    source = board_path.read_bytes()
    text = source.decode("utf-8")
    target_source_hits = {
        "refs": {
            ref: len(re.findall(r'\(property "Reference" "' + re.escape(ref) + r'"', text))
            for ref in sorted(TARGET_REFS)
        },
        "nets": {net: text.count(net) for net in sorted(TARGET_NETS)},
    }

    return {
        "board_path": str(board_path),
        "sha256": hashlib.sha256(source).hexdigest(),
        "counts": {
            "footprints": len(footprints),
            "pads": len(pads),
            "segments": sum(value["kind"] == "segment" for value in copper.values()),
            "vias": sum(value["kind"] == "via" for value in copper.values()),
            "zones": len(zones),
            "nets": len(board.GetNetsByName()),
            "connectivity_pairs": len(pairs),
        },
        "footprints": footprints,
        "pads": pads,
        "copper": copper,
        "zones": zones,
        "net_names": sorted(str(name) for name in board.GetNetsByName().keys()),
        "connectivity_pairs": [list(pair) for pair in sorted(pairs)],
        "target_source_hits": target_source_hits,
    }


def compare(baseline, after, manifest):
    baseline = json.loads(Path(baseline).read_text())
    after = json.loads(Path(after).read_text())
    manifest = json.loads(Path(manifest).read_text())
    expected_removed = set(manifest["object_uuids"])

    retained_refs = set(baseline["footprints"]) - TARGET_REFS
    retained_pad_uids = {
        pad_uid for pad_uid, record in baseline["pads"].items() if record["ref"] not in TARGET_REFS
    }
    expected_pairs = {
        tuple(pair)
        for pair in baseline["connectivity_pairs"]
        if pair[0] in retained_pad_uids and pair[1] in retained_pad_uids
    }
    actual_pairs = {tuple(pair) for pair in after["connectivity_pairs"]}
    before_objects = set(baseline["footprints"].get(ref, {}).get("uuid", "") for ref in baseline["footprints"])
    before_objects |= set(baseline["copper"])
    before_objects.discard("")
    after_objects = set(after["footprints"].get(ref, {}).get("uuid", "") for ref in after["footprints"])
    after_objects |= set(after["copper"])
    after_objects.discard("")

    modified_pad = manifest["modified_pad"]
    modified_pad_uuid = modified_pad["uuid"]
    expected_modified_pad = dict(baseline["pads"][modified_pad_uuid])
    expected_modified_pad["net"] = modified_pad["after_net"]
    modified_copper = {}
    for record in manifest.get("modified_copper", []):
        expected = dict(baseline["copper"][record["uuid"]])
        expected.update(record["after"])
        modified_copper[record["uuid"]] = expected
    retained_copper_uids = set(baseline["copper"]) - expected_removed
    checks = {
        "target_refs_absent": not (set(after["footprints"]) & TARGET_REFS),
        "target_nets_absent": not (set(after["net_names"]) & TARGET_NETS),
        "target_tokens_absent_from_board_source": all(
            count == 0
            for group in after["target_source_hits"].values()
            for count in group.values()
        ),
        "retained_footprints_exact": set(after["footprints"]) == retained_refs,
        "retained_footprint_records_exact": all(
            after["footprints"].get(ref) == baseline["footprints"][ref] for ref in retained_refs
        ),
        "retained_pads_exact": set(after["pads"]) == retained_pad_uids,
        "retained_pad_records_exact": all(
            after["pads"].get(pad_uid) == baseline["pads"][pad_uid]
            for pad_uid in retained_pad_uids
            if pad_uid != modified_pad_uuid
        ),
        "j1_6_changed_only_to_nc": after["pads"].get(modified_pad_uuid) == expected_modified_pad,
        "retained_copper_records_exact": all(
            after["copper"].get(uid) == baseline["copper"][uid]
            for uid in retained_copper_uids
            if uid not in modified_copper
        ),
        "declared_copper_modifications_exact": all(
            after["copper"].get(uid) == expected for uid, expected in modified_copper.items()
        ),
        "retained_connectivity_pairs_exact": actual_pairs == expected_pairs,
        "zones_and_keepouts_exact": after["zones"] == baseline["zones"],
        "removed_object_uuids_exact": before_objects - after_objects == expected_removed,
        "no_unexpected_new_object_uuids": not (after_objects - before_objects),
    }
    report = {
        "pass": all(checks.values()),
        "checks": checks,
        "expected_removed_object_uuids": sorted(expected_removed),
        "actual_removed_object_uuids": sorted(before_objects - after_objects),
        "unexpected_new_object_uuids": sorted(after_objects - before_objects),
        "missing_retained_pairs": [list(pair) for pair in sorted(expected_pairs - actual_pairs)],
        "unexpected_retained_pairs": [list(pair) for pair in sorted(actual_pairs - expected_pairs)],
        "baseline_counts": baseline["counts"],
        "after_counts": after["counts"],
    }
    return report


def write_json(path, data):
    Path(path).write_text(json.dumps(data, ensure_ascii=False, indent=2, sort_keys=True) + "\n")


def negative_controls(baseline_path, after_path, manifest_path):
    after = json.loads(Path(after_path).read_text())
    results = {}
    with tempfile.TemporaryDirectory() as directory:
        directory = Path(directory)

        target = json.loads(json.dumps(after))
        target["footprints"]["U20"] = {"uuid": "negative-control-restored-u20"}
        target["net_names"].append("/P24_PRESENT")
        target["target_source_hits"]["refs"]["U20"] = 1
        target["target_source_hits"]["nets"]["/P24_PRESENT"] = 1
        target_path = directory / "restored_target.json"
        write_json(target_path, target)
        target_report = compare(baseline_path, target_path, manifest_path)
        results["restored_target_rejected"] = (
            not target_report["pass"]
            and not target_report["checks"]["target_refs_absent"]
            and not target_report["checks"]["target_nets_absent"]
            and not target_report["checks"]["target_tokens_absent_from_board_source"]
        )

        broken = json.loads(json.dumps(after))
        removed_pair = broken["connectivity_pairs"].pop(0)
        broken_path = directory / "broken_retained_connection.json"
        write_json(broken_path, broken)
        broken_report = compare(baseline_path, broken_path, manifest_path)
        results["broken_retained_connection_rejected"] = (
            not broken_report["pass"]
            and not broken_report["checks"]["retained_connectivity_pairs_exact"]
            and removed_pair in broken_report["missing_retained_pairs"]
        )

    return {
        "pass": all(results.values()),
        "checks": results,
        "expected_failures": ["restored target ref/net", "broken retained connectivity pair"],
    }


def main():
    if len(sys.argv) < 2:
        raise SystemExit("usage: audit_no_p24.py snapshot BOARD OUT | compare BASELINE AFTER MANIFEST OUT | negative-controls BASELINE AFTER MANIFEST OUT")
    if sys.argv[1] == "snapshot" and len(sys.argv) == 4:
        write_json(sys.argv[3], snapshot(sys.argv[2]))
        return
    if sys.argv[1] == "compare" and len(sys.argv) == 6:
        report = compare(sys.argv[2], sys.argv[3], sys.argv[4])
        write_json(sys.argv[5], report)
        print(json.dumps(report["checks"], ensure_ascii=False, sort_keys=True))
        raise SystemExit(0 if report["pass"] else 1)
    if sys.argv[1] == "negative-controls" and len(sys.argv) == 6:
        report = negative_controls(sys.argv[2], sys.argv[3], sys.argv[4])
        write_json(sys.argv[5], report)
        print(json.dumps(report["checks"], ensure_ascii=False, sort_keys=True))
        raise SystemExit(0 if report["pass"] else 1)
    raise SystemExit("invalid arguments")


if __name__ == "__main__":
    main()
