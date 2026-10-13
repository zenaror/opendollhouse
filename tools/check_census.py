#!/usr/bin/env python3
"""Compare a native census to the independently collected 2026-10-13 snapshot.

These counts describe this corpus, including copies; they are not universal TS1
requirements. This check does not authenticate inputs or measure compatibility.
"""
import json
import sys
from pathlib import Path

EXPECTED = {
    "far_files": 90,
    "far_entries": 43723,
    "far_unique_normalized_names": 43573,
    "far_repeated_normalized_names": 115,
    "iff_containers": 3570,
    "bhav_routines": 28778,
    "bhav_instructions": 401238,
    "distinct_primitive_opcodes": 49,
    "spr2_frames": 143487,
    "spr2_frames_with_z_flag": 142707,
    "spr2_frames_with_alpha_flag": 135582,
    "spr2_rectangular_pixels": 437512628,
    "spr2_rectangular_z_pixels": 434784272,
    "errors": 0,
}


def main():
    if len(sys.argv) != 2:
        raise SystemExit("Usage: python3 tools/check_census.py REPORT.json")
    report = json.loads(Path(sys.argv[1]).read_text(encoding="utf-8"))
    if report.get("schema_version") != 1 or report.get("evidence") != "static_structure_only":
        raise SystemExit("Unsupported census schema/evidence label")
    totals = report.get("totals", {})
    differences = [
        f"{key}: expected {value}, observed {totals.get(key)!r}"
        for key, value in EXPECTED.items()
        if totals.get(key) != value
    ]
    if differences:
        raise SystemExit("Corpus differs from snapshot:\n" + "\n".join(differences))
    print(f"{len(EXPECTED)} corpus counters match the independent static snapshot")


if __name__ == "__main__":
    main()
