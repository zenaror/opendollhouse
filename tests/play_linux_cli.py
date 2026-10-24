#!/usr/bin/env python3
"""Checks that the convenience launcher uses the integrated game entry."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile


SCRIPT = Path(__file__).resolve().parents[1] / "tools/play_linux.py"


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
        game.mkdir()
        argument_log = root / "person-args.json"
        person = binary_dir / "odh-person"
        person.write_text("#!/usr/bin/env python3\n"
                          "import json, pathlib, sys\n"
                          f"pathlib.Path({str(argument_log)!r}).write_text(json.dumps(sys.argv[1:]))\n"
                          "print('integrated-entry-started')\n")
        person.chmod(0o755)

        planned = run(binary_dir, game, "--house", "7", "--language", "15",
                      "--resident-id", "47", "--plan-only")
        assert planned.returncode == 0, planned.stderr
        assert planned.stdout.strip() == "integrated-entry-started"
        assert json.loads(argument_log.read_text()) == [
            "--game-root", str(game.resolve()), "--mode", "1",
            "--language", "15", "--house", "7", "--resident-id", "47", "--plan-only"]

        interactive = run(binary_dir, game)
        assert interactive.returncode == 0, interactive.stderr
        assert json.loads(argument_log.read_text()) == [
            "--game-root", str(game.resolve()), "--mode", "1"]

        direct = run(binary_dir, game, "--house", "7", "--resident-id", "48")
        assert direct.returncode == 0, direct.stderr
        assert json.loads(argument_log.read_text()) == [
            "--game-root", str(game.resolve()), "--mode", "1",
            "--house", "7", "--resident-id", "48"]

        save = root / "mode-1-house-7-family-5.odhs"
        save.write_bytes(b"synthetic save")
        resumed = subprocess.run([sys.executable, str(SCRIPT), "--binary-dir", str(binary_dir),
            "--game-root", str(game), "--load-session", str(save)], text=True,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        assert resumed.returncode == 0, resumed.stderr
        assert json.loads(argument_log.read_text()) == [
            "--game-root", str(game.resolve()), "--load-session", str(save.resolve())]

        invalid_plan = run(binary_dir, game, "--plan-only")
        assert invalid_plan.returncode == 2 and "--plan-only requires --house" in invalid_plan.stderr
        invalid_session_plan = run(binary_dir, game, "--house", "7", "--load-session", str(save), "--plan-only")
        assert invalid_session_plan.returncode == 2 and "cannot be combined with --load-session" in invalid_session_plan.stderr
        invalid_resident = run(binary_dir, game, "--resident-id", "99999")
        assert invalid_resident.returncode == 2 and "--resident-id must be in 1..32767" in invalid_resident.stderr
        invalid_mode = subprocess.run([sys.executable, str(SCRIPT), "--binary-dir", str(binary_dir),
            "--game-root", str(game), "--mode", "6"], text=True,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        assert invalid_mode.returncode == 2 and "invalid choice" in invalid_mode.stderr

    print("Linux convenience launcher forwarding checks passed")


if __name__ == "__main__":
    main()
