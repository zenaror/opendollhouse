#!/usr/bin/env python3
"""Start the integrated Linux game flow using the selected TS1 data folder."""
import argparse
from pathlib import Path
import subprocess
import sys


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game-root", required=True, type=Path)
    parser.add_argument("--binary-dir", type=Path,
                        default=Path(__file__).resolve().parents[1] / "build" / "linux-checkpoint")
    parser.add_argument("--mode", type=int, choices=(1, 2, 3, 4, 5, 7),
                        help="neighborhood mode; inferred from --load-session when resuming")
    parser.add_argument("--language", type=int, choices=range(1, 21),
                        help="game language code; omit to use the saved preference or choose at launch")
    parser.add_argument("--house", type=int, choices=range(1, 99),
                        help="open a specific house directly instead of showing the neighborhood")
    parser.add_argument("--resident-id", type=int,
                        help="select a neighborhood resident directly")
    parser.add_argument("--load-session", type=Path,
                        help="resume an OpenDollhouse .odhs save instead of opening a new session")
    parser.add_argument("--plan-only", action="store_true",
                        help="resolve a specific house without opening a window; requires --house")
    args = parser.parse_args()

    if args.mode is None and args.load_session is None:
        parser.error("--mode is required unless --load-session is provided")
    if args.plan_only and args.house is None:
        parser.error("--plan-only requires --house")
    if args.plan_only and args.load_session is not None:
        parser.error("--plan-only cannot be combined with --load-session")
    if args.resident_id is not None and not 1 <= args.resident_id <= 32767:
        parser.error("--resident-id must be in 1..32767")

    game = args.game_root.resolve()
    binary = args.binary_dir.resolve() / "odh-person"
    if not binary.is_file():
        print(f"odh-play-linux: required OpenDollhouse program is missing: {binary}",
              file=sys.stderr)
        return 2

    command = [str(binary), "--game-root", str(game)]
    if args.mode is not None:
        command += ["--mode", str(args.mode)]
    if args.language is not None:
        command += ["--language", str(args.language)]
    if args.house is not None:
        command += ["--house", str(args.house)]
    if args.resident_id is not None:
        command += ["--resident-id", str(args.resident_id)]
    if args.load_session is not None:
        command += ["--load-session", str(args.load_session.resolve())]
    if args.plan_only:
        command.append("--plan-only")
    try:
        return subprocess.run(command, check=False).returncode
    except OSError as error:
        print(f"odh-play-linux: could not start {binary}: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
