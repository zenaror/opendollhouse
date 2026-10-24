#!/usr/bin/env python3
"""Checks the Linux map -> selected household -> saved-lot launch handoff."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile


SCRIPT = Path(__file__).resolve().parents[1] / "tools/play_linux.py"


def fake_executable(path, body):
    path.write_text("#!/usr/bin/env python3\n" + body)
    path.chmod(0o755)


def run(binary_dir, game, *args):
    return subprocess.run([sys.executable, str(SCRIPT), "--binary-dir", str(binary_dir),
                           "--game-root", str(game), "--mode", "1", *args],
                          text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)


def main():
    with tempfile.TemporaryDirectory(prefix="odh-play-linux-") as temporary:
        root = Path(temporary)
        binary_dir = root / "bin"
        binary_dir.mkdir()
        game = root / "game"
        paths = [
            "UIGraphics/UIGraphics.far", "UserData/Neighborhood.iff",
            "UserData/LotZoning.iff", "UserData/Houses/House07.iff",
            "UserData/Characters/User00000.iff", "UserData/Characters/User00001.iff",
            "GameData/Global/Global.far", "GameData/Objects/Objects.far",
            "GameData/Animation/Animation.far", "GameData/Textures/Textures.far",
            "GameData/floors.iff", "GameData/walls.iff", "ExpansionShared/ExpansionShared.far",
            "GameData/Floors/test.flr", "GameData/Walls/test.wll",
        ]
        for name in paths:
            path = game / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(b"fixture")
        (root / "map.json").write_text(json.dumps({"status": "selected", "mode": 1,
                                                       "selected_house": 7}))
        (root / "cancel.json").write_text(json.dumps({"status": "cancelled", "mode": 1,
                                                          "selected_house": None}))
        (root / "session.json").write_text(json.dumps({
            "evidence": "session_entry_resolved_only", "mode": 1, "house_number": 7,
            "family_chunk_id": 12, "members": [
                {"guid": 850010198, "neighbor_id": 48, "character_asset": "User00000.iff", "object_definition_id": 128},
                {"guid": 844195932, "neighbor_id": 47, "character_asset": "User00001.iff", "object_definition_id": 128},
            ]}))
        (root / "person-args.json").write_text("[]")
        fake_executable(binary_dir / "odh-neighborhood-window", f"""
import pathlib
print(pathlib.Path({str(root / 'map.json')!r}).read_text())
""")
        fake_executable(binary_dir / "odh-session-entry", f"""
import json, pathlib
print(pathlib.Path({str(root / 'session.json')!r}).read_text())
""")
        fake_executable(binary_dir / "odh-person", f"""
import json, pathlib, sys
pathlib.Path({str(root / 'person-args.json')!r}).write_text(json.dumps(sys.argv[1:]))
with pathlib.Path({str(root / 'person-launches.jsonl')!r}).open("a") as launches:
    launches.write(json.dumps(sys.argv[1:]) + "\\n")
""")

        planned = run(binary_dir, game, "--plan-only")
        assert planned.returncode == 0, planned.stderr
        report = json.loads(planned.stdout)
        assert report["status"] == "ready" and report["house"] == 7
        assert report["family_chunk_id"] == 12 and report["resident_guid"] == 850010198
        assert report["resident_neighbor_id"] == 48
        command = report["command"]
        assert command[command.index("--avatar-file") + 1].endswith("User00000.iff")
        assert command[command.index("--render-saved-guid") + 1] == "850010198"
        assert "--render-saved-avatar" not in command
        assert command.count("--object-definition-file") == 2
        assert "--saved-world-main" in command and "--render-window" in command
        size_index = command.index("--render-size")
        assert command[size_index + 1:size_index + 3] == ["800", "600"]
        assert command.count("--architecture-resource") == 2

        other = run(binary_dir, game, "--resident-id", "47", "--plan-only")
        assert other.returncode == 0, other.stderr
        assert json.loads(other.stdout)["resident_guid"] == 844195932
        assert json.loads(other.stdout)["resident_neighbor_id"] == 47
        assert "User00001.iff" in json.loads(other.stdout)["resident_asset"]

        launched = run(binary_dir, game, "--resident-id", "47", "--house", "7")
        assert launched.returncode == 0, launched.stderr
        assert json.loads((root / "person-args.json").read_text()) == json.loads(
            run(binary_dir, game, "--resident-id", "47", "--plan-only").stdout)["command"][1:]

        # Closing a lot returns to the neighborhood; Escape there exits cleanly.
        (root / "picker-count.txt").write_text("0")
        fake_executable(binary_dir / "odh-neighborhood-window", f"""
import json, pathlib
count_path = pathlib.Path({str(root / 'picker-count.txt')!r})
count = int(count_path.read_text()) + 1
count_path.write_text(str(count))
response = {str(root / 'map.json')!r} if count <= 2 else {str(root / 'cancel.json')!r}
print(pathlib.Path(response).read_text())
""")
        (root / "person-launches.jsonl").write_text("")
        returned = run(binary_dir, game)
        assert returned.returncode == 0, returned.stderr
        stages = [json.loads(line) for line in returned.stdout.splitlines()]
        assert [stage["status"] for stage in stages] == [
            "starting_lot_preview", "starting_lot_preview", "cancelled"]
        assert [stage["house"] for stage in stages[:2]] == [7, 7]
        assert int((root / "picker-count.txt").read_text()) == 3
        assert len((root / "person-launches.jsonl").read_text().splitlines()) == 2

        # Replace the fake picker response to exercise the cancel path.
        person_args_before_cancel = (root / "person-args.json").read_text()
        (binary_dir / "odh-neighborhood-window").write_text(
            "#!/usr/bin/env python3\nimport json, pathlib\nprint(pathlib.Path(" +
            repr(str(root / "cancel.json")) + ").read_text())\n")
        (binary_dir / "odh-neighborhood-window").chmod(0o755)
        cancelled = run(binary_dir, game)
        assert cancelled.returncode == 0 and json.loads(cancelled.stdout)["status"] == "cancelled"
        assert (root / "person-args.json").read_text() == person_args_before_cancel

        invalid = run(binary_dir, game, "--resident-id", "999", "--plan-only", "--house", "7")
        assert invalid.returncode == 2 and not invalid.stdout and "not in the selected family" in invalid.stderr

    print("Linux game launch handoff checks passed")


if __name__ == "__main__":
    main()
