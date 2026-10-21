#!/usr/bin/env python3
"""Optional resource-backed Door Route45 probe; commercial game data stays external."""
import argparse
import json
import subprocess
from pathlib import Path


def probe(binary: Path, game_root: Path, attribute1: int) -> dict:
    objects = game_root / "GameData/Objects/Objects.far"
    global_far = game_root / "GameData/Global/Global.far"
    command = [
        str(binary), str(objects), "Doors.iff", "4113", "--run", "--global",
        str(global_far), "Global.iff", "--world-probe", "--route-probe",
        "--route-slot-probe", "--object-def", "3114", "--route-attribute1",
        str(attribute1),
    ]
    result = subprocess.run(command, check=True, capture_output=True, text=True)
    return json.loads(result.stdout)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, default=Path("./odh-bhav"))
    parser.add_argument("--game-root", type=Path, required=True,
                        help="root containing GameData/Objects and GameData/Global")
    args = parser.parse_args()
    binary = args.binary.resolve()
    game_root = args.game_root.resolve()

    expected = {0: (2, 16), 1: (1, 1)}  # attr1 -> selected BHAV pc -> Route45 arrival flags
    for value, (branch_pc, entry_flags) in expected.items():
        data = probe(binary, game_root, value)
        branch_trace = [
            event["instruction"]["pc"] for event in data["trace"]
            if event["routine"] == 4113
        ]
        binding = data["stackobject_slot_binding"]
        assert binding["objd_id"] == 3114 and binding["slot_table"] == 128, binding
        assert binding["initial_attribute_1"] == value, binding
        assert branch_trace == [0, branch_pc], branch_trace
        assert data["stackobject_slot_resolutions"] == 1, data
        assert data["status"] == "returned_true", data
        assert data["route_ticks"] > 0 and data["movements"], data
        assert data["route_entry_flags"] == entry_flags, data
        print(json.dumps({
            "attribute1": value,
            "door_objd": binding["objd_id"],
            "slot_table": binding["slot_table"],
            "door_bhav_branch_pc": branch_pc,
            "slot_resolutions": data["stackobject_slot_resolutions"],
            "route_ticks": data["route_ticks"],
            "final_position": data["final_position"],
            "route_entry_flags": data["route_entry_flags"],
            "evidence": "real BHAV/SLOT resources in constructed ODH world",
        }, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
