#!/usr/bin/env python3
"""Run the TS1 clock/needs contract against the player's external House07 data."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

from linux_household_session_resume_cli import archive_sections
from house07_resident_main_smoke import RESIDENTS


def main():
    if len(sys.argv) != 3:
        raise SystemExit(f"usage: {sys.argv[0]} ODH_PERSON GAME_ROOT")
    binary = Path(sys.argv[1]).resolve()
    game = Path(sys.argv[2]).resolve()
    inputs = [game / "UserData/Houses/House07.iff",
              game / "UserData/Neighborhood.iff"]
    inputs.extend(game / resident[2] for resident in RESIDENTS)
    before = {str(path.relative_to(game)): hashlib.sha256(path.read_bytes()).hexdigest()
              for path in inputs}

    with tempfile.TemporaryDirectory(prefix="odh-house07-clock-") as temp:
        base = Path(temp)
        env = os.environ.copy()
        for key in ("XDG_CONFIG_HOME", "XDG_STATE_HOME", "XDG_DATA_HOME"):
            env[key] = str(base / key)
        env.pop("DISPLAY", None)
        env.pop("WAYLAND_DISPLAY", None)

        def run(ticks, load=None):
            command = [str(binary), "--game-root", str(game)]
            if load:
                command += ["--load-session", str(load)]
            else:
                command += ["--mode", "1", "--house", "7", "--resident-id", "48",
                            "--language", "1"]
            command += ["--headless-ticks", str(ticks), "--save-session-on-exit"]
            result = subprocess.run(command, capture_output=True, text=True,
                                    env=env, timeout=240)
            if result.returncode != 0 or not result.stdout.strip():
                raise AssertionError((result.returncode, result.stdout[-2000:],
                                       result.stderr[-2000:]))
            report = json.loads(result.stdout)
            assert report["tick_budget_exhausted"] and not report["host_failed"], report
            assert report["simulation"]["profile"] == "Simitone_default_TS1_reference", report["simulation"]
            return report

        first_tick = run(1)
        first_clock = first_tick["simulation"]["clock"]
        assert (first_clock["ticks"], first_clock["hour"], first_clock["minute"],
                first_clock["minute_fraction"]) == (1, 7, 1, 0), first_clock

        checkpoint = run(31)
        checkpoint_clock = checkpoint["simulation"]["clock"]
        assert (checkpoint_clock["ticks"], checkpoint_clock["hour"],
                checkpoint_clock["minute"], checkpoint_clock["minute_fraction"]) == (31, 7, 2, 0), checkpoint_clock
        save_path = Path(checkpoint["saved_session_path"])
        save_sections = {ident: body for ident, _, body in archive_sections(save_path.read_bytes())}
        assert b"SIMS" in save_sections, "saved session did not include the simulation clock/carries"

        resumed = run(300, save_path)
        direct = run(331)
        resumed_sim = resumed["simulation"]
        direct_sim = direct["simulation"]
        assert resumed_sim == direct_sim, "clock or motive carries differ after save/resume"
        assert direct_sim["clock"]["ticks"] == 331 and direct_sim["clock"]["minute"] == 12, direct_sim["clock"]
        updated_ids = {avatar["id"] for avatar in direct_sim["avatars"]
                       if avatar["status"] == "updated"}
        assert updated_ids == {105, 137}, (updated_ids, direct_sim["avatars"])
        assert any(avatar["needs"] != before_avatar["needs"]
                   for avatar, before_avatar in zip(direct_sim["avatars"], checkpoint["simulation"]["avatars"])), (
                       "no House07 need changed during the bounded clock run", direct_sim["avatars"])

    after = {str(path.relative_to(game)): hashlib.sha256(path.read_bytes()).hexdigest()
             for path in inputs}
    assert before == after, "the external game files changed during the test"
    print(json.dumps({
        "evidence": "external_House07_clock_needs_save_resume",
        "first_tick_clock": first_clock,
        "checkpoint_clock": checkpoint_clock,
        "continued_clock": direct_sim["clock"],
        "updated_avatars": sorted(updated_ids),
        "needs_changed": True,
        "resume_matches_continuous": True,
        "SIMS_section_saved": True,
        "external_inputs_unchanged": True,
        "visual_window_validated": False,
        "full_gameplay": False,
    }, indent=2))


if __name__ == "__main__":
    main()
