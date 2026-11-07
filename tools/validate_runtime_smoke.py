#!/usr/bin/env python3
"""Failure-first, fresh-process development smoke. Never final acceptance."""
import argparse
import concurrent.futures
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import threading
import time
from datetime import datetime, timezone

MANAGED = {"--game-root", "--load-session", "--headless-ticks", "--instruction-budget",
           "--save-session-on-exit", "--headless-dialog-ok"}
SCHEMA = "development_runtime_smoke/v1"


def digest(path):
    h = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def within(path, root):
    # Path.is_relative_to requires Python 3.9; the project also supports 3.8.
    try:
        path.relative_to(root)
        return True
    except ValueError:
        return False


def profile_files(root):
    """Exact regular-file inventory; never follow a profile symlink outside isolation."""
    root = Path(root)
    if not root.is_dir() or root.is_symlink():
        raise ValueError("profile source must be a real directory")
    result = {}
    for item in sorted(root.rglob("*")):
        if item.is_symlink():
            raise ValueError("profile source contains a symlink: " + str(item))
        if item.is_file():
            result[str(item.relative_to(root))] = digest(item)
        elif not item.is_dir():
            raise ValueError("profile source contains a nonregular entry: " + str(item))
    return result


def profile_checkpoint(root, files, checkpoint_hash):
    if not any(Path(name).suffix == ".odhp" for name in files):
        raise ValueError("profile source lacks neighborhood occupancy (.odhp)")
    matches = [name for name, sha in files.items() if Path(name).suffix == ".odhs" and sha == checkpoint_hash]
    if len(matches) != 1:
        raise ValueError("profile source lacks a unique checkpoint matching the requested save")
    return Path(root) / matches[0]


def integer(value):
    return isinstance(value, int) and not isinstance(value, bool)


def pointer(document, path):
    if not path.startswith("/"):
        raise ValueError("integer requirement must use a JSON pointer starting with /")
    value = document
    for token in path[1:].split("/"):
        token = token.replace("~1", "/").replace("~0", "~")
        value = value[int(token)] if isinstance(value, list) else value[token]
    return value


def protected_files(path, game):
    data = json.loads(Path(path).read_text())
    if not isinstance(data, dict):
        raise ValueError("protected manifest must be a map")
    data = data.get("protected_before", data.get("files", data))
    if not isinstance(data, dict) or not data:
        raise ValueError("protected manifest must contain a nonempty file/hash map")
    result = {}
    for relative, sha in data.items():
        item = Path(relative)
        if item.is_absolute() or ".." in item.parts or not item.parts or not within((game / item).resolve(), game):
            raise ValueError("protected file must stay within the game root")
        if not isinstance(sha, str) or len(sha) != 64 or any(c not in "0123456789abcdef" for c in sha):
            raise ValueError("protected file digest must be lowercase SHA256")
        result[relative] = sha
    return result


def parser():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--binary", required=True, type=Path)
    p.add_argument("--launcher-arg", action="append", default=[])
    p.add_argument("--game-root", required=True, type=Path)
    p.add_argument("--checkpoint", required=True, type=Path)
    p.add_argument("--capture-ticks", required=True, type=int)
    p.add_argument("--total-ticks", required=True, type=int)
    p.add_argument("--protected-manifest", required=True, type=Path)
    p.add_argument("--output", required=True, type=Path)
    p.add_argument("--jobs", choices=(1, 2), default=2, type=int)
    p.add_argument("--timeout-seconds", type=float, default=180)
    p.add_argument("--require-integer", nargs=2, action="append", default=[], metavar=("POINTER", "MIN"))
    p.add_argument("--input-args", nargs=argparse.REMAINDER, default=[])
    return p


class Smoke:
    def __init__(self, args):
        self.args = args
        self.output = args.output
        self.binary_hash = digest(args.binary)
        self.input_hash = digest(args.checkpoint)
        self.protected = protected_files(args.protected_manifest, args.game_root)
        self.result = {"schema": SCHEMA, "purpose": "iteration_only", "final_acceptance": False,
                       "dev_smoke_pass": False, "binary_sha256": self.binary_hash,
                       "checkpoint_sha256": self.input_hash, "protected_count": len(self.protected),
                       "requirements": args.require_integer, "processes": {}, "comparisons": {},
                       "blocked_phases": [], "errors": []}

    def save_status(self):
        # Only this new output directory is writable; atomic status publication
        # permits inspection after interruption without treating it as success.
        temporary = self.output / "status.pending"
        temporary.write_text(json.dumps(self.result, indent=2) + "\n")
        temporary.replace(self.output / "result.json")

    def originals(self):
        return {name: digest(self.args.game_root / name) for name in self.protected}

    def run(self, name, source, ticks, extras, requirements, expected_base=None, stop=None, expected_source_hash=None,
            automatic_dialog_ok=True, report_validator=None, overall_deadline=None,
            profile_source=None, expected_profile_hashes=None):
        directory = self.output / name
        directory.mkdir()
        profile = directory / "profile"
        copied = profile / "input.odhs"
        record = {"phase": name, "ok": False, "errors": [], "started_utc": datetime.now(timezone.utc).isoformat(),
                  "source": str(source), "requested_ticks": ticks}
        start = time.monotonic()
        process = None
        source_hash = None
        source_profile_before = None
        copied_profile_before = None
        try:
            source_hash = digest(source)
            if expected_source_hash is not None and source_hash != expected_source_hash:
                raise ValueError("source checkpoint differs from the recorded producer hash")
            if profile_source is None:
                profile.mkdir()
                shutil.copyfile(source, copied)
            else:
                profile_source = Path(profile_source)
                record["source_profile"] = str(profile_source)
                source_profile_before = profile_files(profile_source)
                record["source_profile_before"] = source_profile_before
                if expected_profile_hashes is None or source_profile_before != expected_profile_hashes:
                    raise ValueError("source profile differs from the recorded producer inventory")
                member = profile_checkpoint(profile_source, source_profile_before, source_hash)
                shutil.copytree(profile_source, profile, symlinks=True)
                copied_profile_before = profile_files(profile)
                if copied_profile_before != source_profile_before:
                    raise ValueError("copied profile differs from source inventory")
                copied = profile / member.relative_to(profile_source)
            if digest(copied) != source_hash:
                raise ValueError("copied input differs from source")
            command = [str(self.args.binary)] + self.args.launcher_arg + [
                "--game-root", str(self.args.game_root), "--load-session", str(copied),
                "--headless-ticks", str(ticks), "--save-session-on-exit"]
            if ticks and automatic_dialog_ok:
                command.append("--headless-dialog-ok")
            command += extras
            record["command"] = command
            env = os.environ.copy()
            for key in ("DISPLAY", "WAYLAND_DISPLAY", "ODH_STATE_DIR"):
                env.pop(key, None)
            for key, child in (("XDG_CONFIG_HOME", "config"), ("XDG_DATA_HOME", "data"),
                               ("XDG_STATE_HOME", "state"), ("XDG_CACHE_HOME", "cache")):
                env[key] = str(profile / child)
            record["xdg_profile"] = str(profile)
            record["binary_sha256_before"] = digest(self.args.binary)
            record["protected_before"] = self.originals()
            if record["binary_sha256_before"] != self.binary_hash or record["protected_before"] != self.protected:
                raise ValueError("binary or protected originals differ before launch")
            deadline = time.monotonic() + self.args.timeout_seconds
            if overall_deadline is not None:
                deadline = min(deadline, overall_deadline)
            if time.monotonic() >= deadline:
                raise ValueError("overall time limit reached before launch")
            with (directory / "stdout.json").open("wb") as stdout, (directory / "stderr.log").open("wb") as stderr:
                process = subprocess.Popen(command, env=env, stdout=stdout, stderr=stderr, shell=False)
                record["pid"] = process.pid
                while process.poll() is None:
                    if stop is not None and stop.is_set():
                        record["errors"].append("blocked by sibling phase failure")
                        process.kill()
                        break
                    if time.monotonic() >= deadline:
                        record["errors"].append("overall time limit reached" if overall_deadline is not None and
                                                time.monotonic() >= overall_deadline else "process timeout")
                        process.kill()
                        break
                    try:
                        process.wait(timeout=0.05)
                    except subprocess.TimeoutExpired:
                        pass
                record["exit"] = process.wait()
            if record["exit"] != 0:
                raise ValueError("application exited nonzero")
            document = json.loads((directory / "stdout.json").read_text(),
                                  parse_constant=lambda value: (_ for _ in ()).throw(ValueError("invalid JSON constant: " + value)))
            if not isinstance(document, dict):
                raise ValueError("application JSON must be an object")
            if report_validator is not None:
                report_validator(document, record)
            if document.get("host_failed") is not False or document.get("instruction_budget_exhausted") is not False:
                raise ValueError("host failed, instruction budget exhausted, or required success fields absent")
            if (document.get("runtime_policy") != "gameplay_per_tick_per_entity" or
                    document.get("runtime_budget_scope") != "per_tick"):
                raise ValueError("runtime did not use the integrated gameplay work policy; diagnostic probes are not a gameplay control")
            yields = document.get("scheduler_budget_yields")
            record["scheduler_budget_yields"] = yields
            record["runtime_policy"] = document.get("runtime_policy")
            if not integer(yields) or yields != 0:
                raise ValueError("gameplay work interrupted an entity for budget reasons, or required yield diagnostics are unavailable")
            base, end = document.get("resumed_tick"), document.get("ticks")
            if not integer(base) or not integer(end) or base < 0 or end != base + ticks or (expected_base is not None and base != expected_base):
                raise ValueError("application horizon mismatch")
            record["resumed_tick"], record["ticks"] = base, end
            for path, minimum in requirements:
                value = pointer(document, path)
                if not integer(value) or value < int(minimum):
                    raise ValueError("required integer branch not observed: " + path)
            if profile_source is None:
                saves = [p for p in profile.rglob("*.odhs") if p != copied]
            else:
                after = profile_files(profile)
                saves = [profile / name for name, sha in after.items() if Path(name).suffix == ".odhs" and
                         copied_profile_before.get(name) != sha]
            if profile_source is not None and ticks == 0 and not saves:
                # A canonical content-addressed checkpoint may already exist in
                # this complete copied profile. Only a zero-tick reopen may
                # reuse that exact input, explicitly reported by the producer.
                reported = document.get("saved_session_path")
                if (expected_base is None or not isinstance(reported, str) or
                        Path(reported).resolve() != copied.resolve() or
                        not within(copied.resolve(), profile.resolve()) or
                        not copied.is_file() or digest(copied) != source_hash):
                    raise ValueError("zero-tick canonical save must report the unchanged copied input exactly")
                saved = copied
                record["canonical_zero_tick_input_reused"] = True
            else:
                if len(saves) != 1:
                    raise ValueError("expected exactly one produced saved session, got " + str(len(saves)))
                saved = saves[0]
            if not saved.is_file() or not within(saved.resolve(), profile.resolve()):
                raise ValueError("saved session escapes the fresh profile")
            if document.get("saved_session_path") is not None and Path(document["saved_session_path"]).resolve() != saved.resolve():
                raise ValueError("reported saved session differs from actual output")
            record["save"] = str(saved)
            record["save_sha256"] = digest(saved)
            record["save_bytes"] = saved.stat().st_size
            if profile_source is not None:
                profile_checkpoint(profile, after, record["save_sha256"])
                record["profile_after"] = after
        except Exception as error:
            record["errors"].append(str(error))
        finally:
            if process is not None and process.poll() is None:
                process.kill()
                process.wait()
            record["pid_absent"] = process is None or (process.poll() is not None and
                (not Path("/proc").is_dir() or not Path("/proc", str(process.pid)).exists()))
            for key, operation in (
                    ("binary_sha256_after", lambda: digest(self.args.binary)),
                    ("protected_after", self.originals),
                    ("copied_input_sha256_after", lambda: digest(copied)),
                    ("source_sha256_after", lambda: digest(source)),
                    ("original_checkpoint_sha256_after", lambda: digest(self.args.checkpoint))):
                try:
                    record[key] = operation()
                except Exception as error:
                    record["errors"].append(key + ": " + str(error))
            if profile_source is not None:
                try:
                    record["source_profile_after"] = profile_files(profile_source)
                except Exception as error:
                    record["errors"].append("source_profile_after: " + str(error))
            record["input_sha256_before"] = source_hash
            record["proofs"] = {
                "binary_unchanged": record.get("binary_sha256_before") == self.binary_hash == record.get("binary_sha256_after"),
                "protected_unchanged": record.get("protected_before") == self.protected == record.get("protected_after"),
                "input_unchanged": source_hash is not None and source_hash == record.get("copied_input_sha256_after") == record.get("source_sha256_after"),
                "original_checkpoint_unchanged": record.get("original_checkpoint_sha256_after") == self.input_hash,
                "pid_absent": record["pid_absent"]}
            if profile_source is not None:
                record["proofs"]["source_profile_unchanged"] = (source_profile_before is not None and
                    source_profile_before == expected_profile_hashes == record.get("source_profile_after"))
            record["ok"] = not record["errors"] and all(record["proofs"].values())
            if not all(record["proofs"].values()):
                record["errors"].append("immutability or process proof failed")
            for filename in ("stdout.json", "stderr.log"):
                if (directory / filename).exists():
                    record[filename + "_sha256"] = digest(directory / filename)
            record["finished_utc"] = datetime.now(timezone.utc).isoformat()
            record["elapsed_seconds"] = time.monotonic() - start
            (directory / "result.json").write_text(json.dumps(record, indent=2) + "\n")
        return record

    def compare(self, name, left, right, expected_left, expected_right):
        a, b = Path(left).read_bytes(), Path(right).read_bytes()
        proof = {"left": str(left), "right": str(right), "left_sha256": hashlib.sha256(a).hexdigest(),
                 "right_sha256": hashlib.sha256(b).hexdigest(), "left_bytes": len(a), "right_bytes": len(b),
                 "actual_bytes_equal": a == b}
        proof["recorded_hashes_match"] = proof["left_sha256"] == expected_left and proof["right_sha256"] == expected_right
        self.result["comparisons"][name] = proof
        return proof["actual_bytes_equal"] and proof["recorded_hashes_match"]

    def execute(self):
        args = self.args
        self.save_status()
        capture = self.run("capture", args.checkpoint, args.capture_ticks, args.input_args, args.require_integer,
                           expected_source_hash=self.input_hash)
        self.result["processes"]["capture"] = capture
        self.save_status()
        if not capture["ok"]:
            self.result["blocked_phases"] = ["reopen", "control", "continue"]
            return False
        reopen = self.run("reopen", Path(capture["save"]), 0, [], [], capture["ticks"],
                          expected_source_hash=capture["save_sha256"])
        self.result["processes"]["reopen"] = reopen
        self.save_status()
        if not reopen["ok"] or not self.compare("zero_tick", capture["save"], reopen["save"],
                                                 capture["save_sha256"], reopen["save_sha256"]):
            self.result["blocked_phases"] = ["control", "continue"]
            return False
        specifications = [("control", args.checkpoint, args.total_ticks, args.input_args,
                           args.require_integer, capture["resumed_tick"]),
                          ("continue", Path(capture["save"]), args.total_ticks - args.capture_ticks,
                           [], [], capture["ticks"])]
        stop = threading.Event()
        with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
            pending = {pool.submit(self.run, *spec, stop=stop,
                       expected_source_hash=self.input_hash if spec[0]=="control" else capture["save_sha256"]): spec[0] for spec in specifications}
            for future in concurrent.futures.as_completed(pending):
                record = future.result()
                self.result["processes"][pending[future]] = record
                if not record["ok"]:
                    stop.set()
                self.save_status()
        if not all(r["ok"] for r in self.result["processes"].values()):
            self.result["blocked_phases"] = ["final_comparison"]
            return False
        control = self.result["processes"]["control"]
        continuation = self.result["processes"]["continue"]
        if control["ticks"] != continuation["ticks"]:
            self.result["errors"].append("final horizons differ")
            return False
        return self.compare("continuation", control["save"], continuation["save"],
                            control["save_sha256"], continuation["save_sha256"])


def main(argv=None):
    p = parser()
    args = p.parse_args(argv)
    if not (0 < args.capture_ticks < args.total_ticks <= 4096) or not (0 < args.timeout_seconds < float("inf")):
        p.error("require 0 < capture-ticks < total-ticks <= 4096 and positive finite timeout")
    for item in args.input_args + args.launcher_arg:
        if item.split("=", 1)[0] in MANAGED:
            p.error("managed CLI option cannot be overridden: " + item)
    for path, minimum in args.require_integer:
        try:
            if not path.startswith("/"):
                raise ValueError()
            int(minimum)
        except ValueError:
            p.error("--require-integer requires /json/pointer and integer minimum")
    for name in ("binary", "game_root", "checkpoint", "protected_manifest", "output"):
        setattr(args, name, getattr(args, name).resolve())
    if within(args.output, Path(__file__).resolve().parents[1]):
        p.error("output must be outside the source checkout")
    if within(args.output, args.game_root):
        p.error("output must be outside the protected game root")
    if args.output.exists():
        p.error("output must be a new directory")
    try:
        smoke = Smoke(args)
    except (OSError, ValueError) as error:
        p.error(str(error))
    args.output.mkdir(parents=True)
    started = time.monotonic()
    try:
        smoke.result["dev_smoke_pass"] = smoke.execute()
    except Exception as error:
        smoke.result["errors"].append(str(error))
    finally:
        smoke.result["elapsed_seconds"] = time.monotonic() - started
        smoke.save_status()
    print(json.dumps(smoke.result, indent=2))
    return 0 if smoke.result["dev_smoke_pass"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
