#!/usr/bin/env python3
"""Compare complete VMTS1SaveDiagnostics records without running the game.

Accepts a diagnostic JSON record, an array of records, or a game log containing
`TS1 save state: ` JSON lines. Multiple matching records require an explicit
zero-based --left-index/--right-index (negative indices are supported).

Examples:
  python3 tools/simitone/compare_save_states.py first.log second.log \
    --left-phase after_serialize --right-phase deserialize_before_load
  python3 tools/simitone/compare_save_states.py saved.json loaded.json --strict

Every State field and component digest is compared. No clock/tick tolerances,
actor filters, expected gameplay mutations or resource remaps are suppressed.
Only State.SIMI.Version 0..0x3f -> 0x40 is classified as allowed format metadata;
it remains in the report. RuntimeFamilySubset is reported as runtime-only, as
the diagnostic schema excludes it from persisted State. --strict exits 1 for
any unexpected difference; otherwise differences are report-only (exit 0).
Input/selection errors exit 2. Nothing is written except JSON on stdout.
"""

import argparse
import copy
import json
from pathlib import Path
import sys

PREFIX = "TS1 save state: "
MISSING = object()


def records_from(path):
    text = Path(path).read_text(encoding="utf-8")
    try:
        data = json.loads(text)
    except json.JSONDecodeError:
        records = []
        for number, line in enumerate(text.splitlines(), 1):
            if PREFIX not in line:
                continue
            try:
                records.append(json.loads(line.split(PREFIX, 1)[1]))
            except json.JSONDecodeError as error:
                raise ValueError(f"{path}:{number}: malformed diagnostic JSON: {error}") from error
        if not records:
            raise ValueError(f"{path}: no diagnostic records found")
        return records
    if isinstance(data, dict):
        return [data]
    if isinstance(data, list) and all(isinstance(record, dict) for record in data):
        return data
    raise ValueError(f"{path}: expected a diagnostic object or array of objects")


def select_record(path, phase=None, index=None):
    records = records_from(path)
    matching = [record for record in records if phase is None or record.get("Phase") == phase]
    if not matching:
        phases = sorted({str(record.get("Phase")) for record in records})
        raise ValueError(f"{path}: no record for phase {phase!r}; available phases: {phases}")
    if index is None:
        if len(matching) != 1:
            raise ValueError(f"{path}: {len(matching)} matching records; choose an explicit occurrence index")
        index = 0
    try:
        record = matching[index]
    except IndexError as error:
        raise ValueError(f"{path}: occurrence {index} out of range for {len(matching)} records") from error
    if record.get("SchemaVersion") != 1:
        raise ValueError(f"{path}: unsupported/missing SchemaVersion {record.get('SchemaVersion')!r}")
    if "DiagnosticError" in record:
        raise ValueError(f"{path}: diagnostic capture failed: {record['DiagnosticError']}")
    if not isinstance(record.get("State"), dict):
        raise ValueError(f"{path}: missing complete State object")
    return record, index, len(matching)


def keyed(items, key, label):
    result = {}
    for item in items:
        identity = key(item)
        if identity in result:
            raise ValueError(f"duplicate {label} identity: {identity}")
        result[identity] = item
    return result


def canonical_state(record):
    state = copy.deepcopy(record["State"])
    # Identify all actors/groups by identity, without discarding fields or
    # changing order inside action queues, stacks, banks or group membership.
    if not isinstance(state.get("Entities"), list) or not isinstance(state.get("Groups"), list):
        raise ValueError("complete State.Entities and State.Groups lists are required")
    state["Entities"] = keyed(state["Entities"], lambda item: str(item["ObjectID"]), "entity ObjectID")
    state["Groups"] = keyed(state["Groups"], lambda item: ",".join(map(str, item["Objects"])), "ordered group members")
    return state


def json_value(value):
    return {"$missing": True} if value is MISSING else value


def pointer_part(key):
    return str(key).replace("~", "~0").replace("/", "~1")


def walk_differences(left, right, path="/State"):
    if left is MISSING and right is MISSING:
        return
    if left is MISSING or right is MISSING:
        yield {"Path": path, "Kind": "missing_left" if left is MISSING else "missing_right",
               "Left": json_value(left), "Right": json_value(right)}
    elif type(left) is not type(right):
        yield {"Path": path, "Kind": "type", "Left": left, "Right": right}
    elif isinstance(left, dict):
        for key in sorted(left.keys() | right.keys()):
            yield from walk_differences(left.get(key, MISSING), right.get(key, MISSING), path + "/" + pointer_part(key))
    elif isinstance(left, list):
        for index in range(max(len(left), len(right))):
            yield from walk_differences(left[index] if index < len(left) else MISSING,
                                       right[index] if index < len(right) else MISSING, path + "/" + str(index))
    elif left != right:
        yield {"Path": path, "Kind": "value", "Left": left, "Right": right}


def compare(left, right):
    differences = list(walk_differences(canonical_state(left), canonical_state(right)))
    for difference in differences:
        if (difference["Path"] == "/State/SIMI/Version" and difference["Kind"] == "value"
                and type(difference["Left"]) is int and 0 <= difference["Left"] < 0x40
                and type(difference["Right"]) is int and difference["Right"] == 0x40):
            difference["Classification"] = "allowed_format_metadata"
            difference["Warning"] = "SIMI writer uses max(inputVersion, 0x40), promoting this legacy version to coherent 64-word version0x40; no data-field difference is allowed by this rule."
        else:
            difference["Classification"] = "unexpected"

    # Header/payload comparisons are explicit, not silently folded into State.
    for key in ("SnapshotVersion", "Compressed", "SerializedPayloadLength", "SerializedPayloadSHA256"):
        left_value, right_value = left.get(key, MISSING), right.get(key, MISSING)
        if key.startswith("SerializedPayload") and (left_value is None or right_value is None):
            continue  # Availability is listed separately below; no bytes to compare.
        for difference in walk_differences(left_value, right_value, "/" + key):
            difference["Classification"] = "unexpected"
            differences.append(difference)
    for key, classification in (("RuntimeFamilySubset", "runtime_only"), ("StateSHA256", "derived_fingerprint")):
        for difference in walk_differences(left.get(key, MISSING), right.get(key, MISSING), "/" + key):
            difference["Classification"] = classification
            differences.append(difference)

    unexpected = sum(difference["Classification"] == "unexpected" for difference in differences)
    return {
        "SchemaVersion": 1,
        "UnexpectedDifferenceCount": unexpected,
        "AllowedFormatMetadataDifferenceCount": sum(difference["Classification"] == "allowed_format_metadata" for difference in differences),
        "RuntimeOnlyDifferenceCount": sum(difference["Classification"] == "runtime_only" for difference in differences),
        "DerivedFingerprintDifferenceCount": sum(difference["Classification"] == "derived_fingerprint" for difference in differences),
        "StrictFieldsEqual": unexpected == 0,
        "PayloadComparison": {
            "LeftSHA256": left.get("SerializedPayloadSHA256"),
            "RightSHA256": right.get("SerializedPayloadSHA256"),
            "Comparable": left.get("SerializedPayloadSHA256") is not None and right.get("SerializedPayloadSHA256") is not None
        },
        "Differences": differences,
        "Limits": [
            "Reads existing diagnostics only; does not load the VM or demonstrate gameplay.",
            "Architecture/routing/animation/async details omitted from readable state are covered by their component digests.",
            "Any differing digest is reported; this tool does not infer or approve expected load/EP2/tick mutations."
        ]
    }


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("left")
    parser.add_argument("right")
    parser.add_argument("--left-phase")
    parser.add_argument("--right-phase")
    parser.add_argument("--left-index", type=int)
    parser.add_argument("--right-index", type=int)
    parser.add_argument("--strict", action="store_true", help="exit 1 for unexpected differences (default: report only)")
    args = parser.parse_args(argv)
    try:
        left, left_index, left_count = select_record(args.left, args.left_phase, args.left_index)
        right, right_index, right_count = select_record(args.right, args.right_phase, args.right_index)
        report = compare(left, right)
        report["Left"] = {"Path": args.left, "Phase": left.get("Phase"), "SelectedIndex": left_index, "MatchingRecordCount": left_count}
        report["Right"] = {"Path": args.right, "Phase": right.get("Phase"), "SelectedIndex": right_index, "MatchingRecordCount": right_count}
        report["Mode"] = "strict" if args.strict else "report_only"
        print(json.dumps(report, indent=2, ensure_ascii=False, allow_nan=False))
        return int(args.strict and not report["StrictFieldsEqual"])
    except (OSError, ValueError, KeyError, TypeError) as error:
        print(json.dumps({"SchemaVersion": 1, "InputError": str(error)}), file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
