#!/usr/bin/env python3
"""Bounded sequential continuity probe; no retries, dialog answers or acceptance claim."""
import argparse
import json
import math
from pathlib import Path
import time
from datetime import datetime, timezone

from validate_runtime_smoke import MANAGED, Smoke, digest, integer, within, profile_files, profile_checkpoint


def parser():
    p = argparse.ArgumentParser(description=__doc__)
    for name in ("binary", "game-root", "checkpoint", "protected-manifest", "output"):
        p.add_argument("--" + name, required=True, type=Path)
    p.add_argument("--launcher-arg", action="append", default=[])
    p.add_argument("--profile-template", type=Path,
                   help="copy this complete XDG profile initially, then chain each produced complete profile")
    p.add_argument("--start-tick", required=True, type=int)
    p.add_argument("--total-ticks", required=True, type=int)
    p.add_argument("--segment-ticks", type=int, default=4096)
    p.add_argument("--max-segments", required=True, type=int)
    p.add_argument("--timeout-seconds", type=float, default=90)
    p.add_argument("--total-timeout-seconds", required=True, type=float)
    p.add_argument("--app-args", nargs=argparse.REMAINDER, default=[],
                   help="static options only: --mode N, --language N, --no-audio")
    return p


def validate_static_args(p, args):
    # Repeated player commands would change the experiment at each boundary.
    # Keep the common argument surface deliberately small; no hidden answers.
    it = iter(args.app_args)
    for token in it:
        if token == "--no-audio":
            continue
        if token not in ("--mode", "--language"):
            p.error("app-args permit only static --mode, --language and --no-audio")
        value = next(it, None)
        if value is None or not value.isdecimal():
            p.error(token + " requires a numeric value")
    for token in args.launcher_arg:
        option = token.split("=", 1)[0]
        if option in MANAGED or option.startswith("--headless-") or option == "--render-window":
            p.error("launcher cannot override runtime options or answer dialogs")


def validate_report(document, record):
    record["reported_tick"] = document.get("ticks")
    record["clock"] = document.get("simulation", {}).get("clock") if isinstance(document.get("simulation"), dict) else None
    record["diagnostics_truncated"] = document.get("diagnostics_truncated")
    if "blocking_dialog" not in document:
        raise ValueError("invalid report: blocking_dialog field absent")
    if document["blocking_dialog"] is not None:
        record["human_decision_required"] = True
        raise ValueError("human decision required: game dialog is waiting; no response supplied")
    for field in ("blocking_dialog_responses", "headless_dialog_responses_requested",
                  "headless_dialog_responses_consumed", "object_main_fault_count"):
        if not integer(document.get(field)) or document[field] != 0:
            raise ValueError("invalid or nonzero report counter: " + field)
    if "registry_first_spatial_invalidation" not in document or document["registry_first_spatial_invalidation"] is not None:
        raise ValueError("spatial integrity invalid or report field absent")
    if document.get("status") not in ("ready", "dormant", "running", "waiting", "yielded", "next_tick", "returned_true", "returned_false"):
        raise ValueError("VM status is failed or unavailable")
    simulation = document.get("simulation")
    if not isinstance(simulation, dict) or not integer(simulation.get("skipped_updates")) or simulation["skipped_updates"] != 0:
        raise ValueError("simulation updates skipped or report invalid")
    clock = simulation.get("clock")
    if not isinstance(clock, dict) or not integer(clock.get("ticks")) or clock["ticks"] != document.get("ticks"):
        raise ValueError("simulation clock disagrees with runtime tick")


class Sequence(Smoke):
    def __init__(self, args):
        super().__init__(args)
        self.profile_source = getattr(args, "profile_template", None)
        self.profile_hashes = profile_files(self.profile_source) if self.profile_source is not None else None
        self.template_hashes = self.profile_hashes
        if self.profile_source is not None:
            profile_checkpoint(self.profile_source, self.profile_hashes, self.input_hash)
        self.result = {
            "schema": "development_runtime_sequence/v1", "purpose": "iteration_only",
            "evidence_scope": "sequential_continuity_only", "final_acceptance": False,
            "sequence_completed": False, "reopening_verified": False,
            "control_equivalence_verified": False, "independent_review": "not_performed",
            "status": "running", "started_utc": datetime.now(timezone.utc).isoformat(),
            "binary_sha256": self.binary_hash, "checkpoint_sha256": self.input_hash,
            "protected_count": len(self.protected), "requested_ticks": args.total_ticks,
            "limits": {key: getattr(args, key) for key in
                       ("start_tick", "segment_ticks", "max_segments", "timeout_seconds", "total_timeout_seconds")},
            "segments": [], "completed_ticks": 0, "errors": [],
            "last_valid_checkpoint": {"path": str(args.checkpoint), "sha256": self.input_hash,
                                      "tick": args.start_tick, "origin": "supplied_input"}}

        if self.profile_source is not None:
            self.result["profile_template"] = str(self.profile_source)
            self.result["profile_template_before"] = self.template_hashes
            self.result["last_valid_profile"] = {"path": str(self.profile_source), "files": self.profile_hashes}

    def checkpoint_intact(self):
        checkpoint = self.result["last_valid_checkpoint"]
        try:
            return digest(Path(checkpoint["path"])) == checkpoint["sha256"]
        except OSError:
            return False

    def execute(self):
        args = self.args
        deadline = time.monotonic() + args.total_timeout_seconds
        self.save_status()
        while self.result["completed_ticks"] < args.total_ticks:
            index = len(self.result["segments"])
            if index >= args.max_segments or time.monotonic() >= deadline:
                self.result["errors"].append("segment or overall time limit reached")
                break
            ticks = min(args.segment_ticks, args.total_ticks - self.result["completed_ticks"])
            checkpoint = self.result["last_valid_checkpoint"]
            name = "segment-%04d" % index
            record = self.run(name, Path(checkpoint["path"]), ticks, args.app_args, [],
                              expected_base=checkpoint["tick"], expected_source_hash=checkpoint["sha256"],
                              automatic_dialog_ok=False, report_validator=validate_report,
                              overall_deadline=deadline, profile_source=self.profile_source,
                              expected_profile_hashes=self.profile_hashes)
            if args.profile_template is not None:
                try:
                    if profile_files(args.profile_template) != self.template_hashes:
                        raise ValueError("original profile template changed during sequence")
                except (OSError, ValueError) as error:
                    record["ok"] = False
                    record["errors"].append(str(error))
                    (self.output / name / "result.json").write_text(json.dumps(record, indent=2) + "\n")
            if record["ok"] and time.monotonic() >= deadline:
                record["ok"] = False
                record["errors"].append("overall time limit reached during stage verification")
                (self.output / name / "result.json").write_text(json.dumps(record, indent=2) + "\n")
            reference = {"phase": name, "ok": record["ok"], "result": str(self.output / name / "result.json"),
                         "ticks": record.get("ticks", record.get("reported_tick")), "errors": record["errors"]}
            self.result["segments"].append(reference)
            if not record["ok"]:
                self.result["status"] = "human_decision_required" if record.get("human_decision_required") else "stopped"
                self.result["errors"].append("stopped at " + name)
                stderr = self.output / name / "stderr.log"
                if stderr.exists():
                    with stderr.open("rb") as stream:
                        stream.seek(max(0, stderr.stat().st_size - 1200))
                        reference["stderr_tail"] = stream.read().decode("utf-8", errors="replace")
                # A nonzero exit can still emit useful JSON. Never promote it,
                # but expose its diagnostic without dumping the complete report.
                try:
                    failed_document = json.loads((self.output / name / "stdout.json").read_text())
                    if isinstance(failed_document, dict):
                        reference["application_diagnostic"] = {
                            key: str(failed_document[key])[:500] for key in
                            ("host_diagnostic", "diagnostic", "last_object_main_fault") if failed_document.get(key)}
                except (ValueError, OSError):
                    pass
            else:
                if self.profile_source is not None:
                    self.profile_source = Path(record["xdg_profile"])
                    self.profile_hashes = record["profile_after"]
                    self.result["last_valid_profile"] = {"path": str(self.profile_source), "files": self.profile_hashes}
                self.result["completed_ticks"] += ticks
                self.result["last_valid_checkpoint"] = {"path": record["save"], "sha256": record["save_sha256"],
                                                        "tick": record["ticks"], "origin": name}
            self.save_status()
            print(json.dumps(reference), flush=True)
            if not record["ok"]:
                break
        if self.result["completed_ticks"] == args.total_ticks:
            self.result.update(sequence_completed=True, status="completed")


def main(argv=None):
    p = parser()
    args = p.parse_args(argv)
    if not (args.start_tick >= 0 and 0 < args.segment_ticks <= 4096 and 0 < args.max_segments <= 1024
            and 0 < args.total_ticks <= args.segment_ticks * args.max_segments):
        p.error("require start>=0, segment1..4096, max-segments1..1024 and total1..segment*max-segments")
    if not all(math.isfinite(x) and x > 0 for x in (args.timeout_seconds, args.total_timeout_seconds)):
        p.error("timeouts must be positive and finite")
    validate_static_args(p, args)
    for name in ("binary", "game_root", "checkpoint", "protected_manifest", "output"):
        setattr(args, name, getattr(args, name).resolve())
    if args.profile_template is not None:
        if args.profile_template.is_symlink():
            p.error("profile template root must not be a symlink")
        args.profile_template = args.profile_template.resolve()
        if within(args.output, args.profile_template) or within(args.profile_template, args.output):
            p.error("output and profile template must not overlap")
    if within(args.output, Path(__file__).resolve().parents[1]) or within(args.output, args.game_root):
        p.error("output must be outside the checkout and protected game root")
    if args.output.exists():
        p.error("output must be a new directory")
    args.require_integer = []  # Shared process runner configuration; no four-phase smoke execution.
    try:
        sequence = Sequence(args)
        if sequence.originals() != sequence.protected:
            raise ValueError("protected originals differ from manifest")
    except (OSError, ValueError) as error:
        p.error(str(error))
    args.output.mkdir(parents=True)
    started = time.monotonic()
    try:
        sequence.execute()
    except Exception as error:
        sequence.result["errors"].append(str(error))
        sequence.result["status"] = "stopped"
    finally:
        if args.profile_template is not None:
            try:
                after = profile_files(args.profile_template)
                sequence.result["profile_template_after"] = after
                unchanged = after == sequence.template_hashes
                last_profile_intact = profile_files(sequence.profile_source) == sequence.profile_hashes
                sequence.result["profile_template_unchanged"] = unchanged
                sequence.result["last_profile_integrity_verified"] = last_profile_intact
                if not unchanged or not last_profile_intact:
                    sequence.result["errors"].append("source template or last complete profile integrity failed")
            except (OSError, ValueError) as error:
                sequence.result["errors"].append("profile integrity: " + str(error))
        intact = sequence.checkpoint_intact()
        sequence.result["last_checkpoint_integrity_verified"] = intact
        if not intact:
            sequence.result["errors"].append("last checkpoint integrity failed; do not use the recorded path")
        if sequence.result["errors"]:
            sequence.result["sequence_completed"] = False
            if sequence.result["status"] == "running":
                sequence.result["status"] = "stopped"
        sequence.result["elapsed_seconds"] = time.monotonic() - started
        sequence.result["finished_utc"] = datetime.now(timezone.utc).isoformat()
        sequence.save_status()
    summary = {key: sequence.result[key] for key in
               ("status", "sequence_completed", "completed_ticks", "last_valid_checkpoint", "errors")}
    summary["summary"] = str(args.output / "result.json")
    print(json.dumps(summary), flush=True)
    return 0 if sequence.result["sequence_completed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
