#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Run checked SDL failures against an already isolated, disposable profile.

Arguments: APP DATA_ROOT ISOLATED_PROFILE INTERPOSER OUTPUT_DIRECTORY.
The profile must contain saves/UserData; no input, save, or physical display is used.
"""
import hashlib
import json
import os
from pathlib import Path
import re
import select
import subprocess
import sys
import time


def manifest(root):
    return {str(p.relative_to(root)): hashlib.sha256(p.read_bytes()).hexdigest()
            for p in root.rglob("*") if p.is_file()}


def close_owned(process):
    if process and process.poll() is None:
        process.terminate()
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait(timeout=5)


def main():
    app, data, profile, interposer, output = map(lambda p: Path(p).resolve(), sys.argv[1:])
    assert profile != data and data not in profile.parents, "profile must be outside game data"
    saves = profile / "saves" / "UserData"
    assert (saves / "Neighborhood.iff").is_file(), "an isolated profile is required"
    output.mkdir(parents=True, exist_ok=False)
    cache = profile / "cache"
    cache.mkdir(exist_ok=True)
    log = cache / "opendollhouse.log"
    before = manifest(saves)
    results = []
    for operation in ("upload", "clear", "copy"):
        row = {"operation": operation, "hard_seconds": 45, "forced_cleanup": False}
        started = time.monotonic()
        virtual = game = None
        read_fd, write_fd = os.pipe()
        try:
            with (output / (operation + "-xvfb.log")).open("w") as errors:
                virtual = subprocess.Popen(
                    ["Xvfb", "-displayfd", str(write_fd), "-screen", "0", "1024x768x24",
                     "-nolisten", "tcp"], pass_fds=(write_fd,), stderr=errors)
            os.close(write_fd)
            write_fd = -1
            assert select.select([read_fd], [], [], 10)[0], "Xvfb did not announce display"
            display = ":" + os.read(read_fd, 30).decode().strip()
            offset = log.stat().st_size if log.exists() else 0
            arm = output / (operation + ".arm")
            env = dict(os.environ, DISPLAY=display, SDL_AUDIODRIVER="dummy",
                       XDG_CONFIG_HOME=str(profile / "xdg-fault"),
                       ODH_PRESENT_FAULT_KIND=operation, ODH_PRESENT_FAULT_ARM=str(arm))
            env["LD_PRELOAD"] = str(interposer) + (
                ":" + env["LD_PRELOAD"] if env.get("LD_PRELOAD") else "")
            with (output / (operation + "-launcher.log")).open("w") as launcher:
                game = subprocess.Popen(
                    [str(app), "--data-root", str(data), "--save-root", str(profile / "saves"),
                     "--cache-dir", str(cache), "--language", "en-US", "--skip-intro", "--silent",
                     "--width", "1024", "--height", "768", "--frames", "1800"],
                    env=env, stdout=launcher, stderr=subprocess.STDOUT)
            row.update(app_pid=game.pid, xvfb_pid=virtual.pid)
            print(json.dumps(dict(event="started", **row)), flush=True)
            armed = False
            while game.poll() is None and time.monotonic() - started < row["hard_seconds"]:
                text = log.read_bytes()[offset:].decode(errors="replace") if log.exists() else ""
                if not armed and re.search(r"desktop: presented frame=\d+ scene=map\b", text):
                    arm.write_text(operation + "\n")
                    armed = True
                time.sleep(0.01)
            if game.poll() is None:
                row["forced_cleanup"] = True  # Hard deadline, not a short observation timeout.
                close_owned(game)
            text = log.read_bytes()[offset:].decode(errors="replace") if log.exists() else ""
            (output / (operation + "-application.log")).write_text(text)
            error = text.find("desktop: presentation failed: presentation fault fixture: " + operation)
            row.update(exit_code=game.returncode, armed=armed, fault_seen=error >= 0,
                       marker_after_error=error >= 0 and "desktop: presented " in text[error:],
                       seconds=time.monotonic() - started)
            row["pass"] = (armed and error >= 0 and game.returncode == 1 and
                           not row["marker_after_error"] and not row["forced_cleanup"])
        except Exception as error:
            row.update(error=repr(error), **{"pass": False})
        finally:
            close_owned(game)
            close_owned(virtual)
            os.close(read_fd)
            if write_fd >= 0:
                os.close(write_fd)
            row.update(app_closed=game is None or game.poll() is not None,
                       xvfb_closed=virtual is None or virtual.poll() is not None)
        results.append(row)
        print(json.dumps(row), flush=True)
    unchanged = manifest(saves) == before
    receipt = {"cases": results, "main_userdata_unchanged": unchanged,
               "main_manifest": before, "scope": "injected SDL failures; no gameplay acceptance"}
    (output / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n")
    return 0 if unchanged and all(row.get("pass") for row in results) else 1


if __name__ == "__main__":
    if len(sys.argv) != 6:
        raise SystemExit(__doc__)
    raise SystemExit(main())
