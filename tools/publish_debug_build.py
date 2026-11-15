#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Publish already verified packages to the single current debug directory."""
from __future__ import annotations
import argparse
import ctypes
import datetime as dt
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import stat
import sys
import tempfile

PLATFORMS = {"linux", "windows", "macos-intel", "macos-arm64", "android", "psvita", "switch", "new3ds", "old3ds", "wiiu"}
KINDS = (".tar.gz", ".tar.xz", ".zip", ".apk", ".vpk", ".nro", ".3dsx", ".cia", ".wuhb", ".rpx")
OWNER = "OpenDollhouse debug publisher"
META = "release.json"


def fail(message: str) -> None:
    raise ValueError(message)


def digest(path: Path) -> str:
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def plain(path: Path, *, directory: bool = False, missing: bool = False) -> None:
    """Reject symlinks in both the selected node and its parent chain."""
    for part in reversed((path,) + tuple(path.parents)):
        if part.is_symlink():
            fail(f"Link is not allowed: {part}")
    try:
        info = path.stat()
    except FileNotFoundError:
        if missing:
            return
        raise
    if not (stat.S_ISDIR(info.st_mode) if directory else stat.S_ISREG(info.st_mode)):
        fail(f"Unexpected file type: {path}")


def make_directory(path: Path) -> None:
    plain(path, directory=True, missing=True)
    path.mkdir(mode=0o700, exist_ok=True)


def filename(name: str, *, artifact: bool = False) -> str:
    if not isinstance(name, str) or not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._+\-]{0,159}", name):
        fail("Package names must be plain filenames")
    if artifact and not name.lower().endswith(KINDS):
        fail(f"Unsupported package type: {name}")
    return name


def read_json(path: Path) -> dict:
    plain(path)
    if path.stat().st_size > 256_000:
        fail(f"Metadata is too large: {path}")
    result = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(result, dict):
        fail("Metadata must be a JSON object")
    return result


def text_field(data: dict, key: str) -> str:
    value = data.get(key)
    if not isinstance(value, str) or not value.strip() or len(value) > 12_000:
        fail(f"Missing or invalid field: {key}")
    return value


def busy(folder: Path) -> None:
    """Check local owned processes before removing a managed package directory."""
    proc = Path("/proc")
    if not proc.is_dir():
        fail("Replacing a package requires the Linux process check (/proc)")
    prefix = str(folder) + "/"
    for process in proc.iterdir():
        if not process.name.isdigit():
            continue
        try:
            if process.stat().st_uid != os.geteuid():
                continue
            names = [process / "exe", process / "cwd"]
            try:
                names.extend((process / "fd").iterdir())
            except (PermissionError, FileNotFoundError, ProcessLookupError):
                pass
            for name in names:
                try:
                    target = os.readlink(name).removesuffix(" (deleted)")
                except (OSError, ValueError):
                    continue
                if target == str(folder) or target.startswith(prefix):
                    fail(f"Package is in use by process {process.name}; close it first")
            try:
                maps = (process / "maps").read_text(errors="replace")
            except (PermissionError, FileNotFoundError, ProcessLookupError):
                maps = ""
            if prefix in maps:
                fail(f"Package is mapped by process {process.name}; close it first")
        except (FileNotFoundError, ProcessLookupError):
            continue


def inventory(folder: Path, platform: str) -> dict:
    plain(folder, directory=True)
    metadata = read_json(folder / META)
    keys = {"schema", "publisher", "channel", "platform", "cohort", "source_revision", "built_at", "published_at", "verification", "instructions", "managed_files"}
    if set(metadata) != keys or metadata.get("channel") != "debug":
        fail("Existing metadata has unknown fields or channel; preserving it")
    if metadata.get("publisher") != OWNER or metadata.get("schema") != 1 or metadata.get("platform") != platform:
        fail("Existing directory does not belong to this debug publisher")
    records = metadata.get("managed_files")
    if not isinstance(records, dict) or not records or len(records) > 17:
        fail("Existing managed-file metadata is invalid")
    for name, record in records.items():
        filename(name, artifact=name != "README.md")
        if name == META or not isinstance(record, dict):
            fail("Invalid managed-file entry")
        path = folder / name
        plain(path)
        if record.get("bytes") != path.stat().st_size or record.get("sha256") != digest(path):
            fail(f"Managed file was changed; preserving it: {name}")
    if {p.name for p in folder.iterdir()} != set(records) | {META}:
        fail("Existing directory contains unknown files; preserving all files, logs and saves")
    busy(folder)
    return metadata


def exchange(staged: Path, current: Path) -> None:
    """One atomic Linux directory exchange; no old->backup visibility gap."""
    if not sys.platform.startswith("linux"):
        fail("Atomic package replacement currently requires a Linux publishing host")
    libc = ctypes.CDLL(None, use_errno=True)
    rename = getattr(libc, "renameat2", None)
    if rename is None:
        fail("Atomic directory exchange is unavailable; current package was preserved")
    rename.argtypes = [ctypes.c_int, ctypes.c_char_p, ctypes.c_int, ctypes.c_char_p, ctypes.c_uint]
    rename.restype = ctypes.c_int
    if rename(-100, os.fsencode(staged), -100, os.fsencode(current), 2):
        code = ctypes.get_errno()
        raise OSError(code, "Atomic directory exchange failed; current package was preserved")


def publish(repo: Path, specification: dict) -> Path:
    if not sys.platform.startswith("linux"):
        fail("This publication helper currently requires a Linux host for atomic replacement")
    keys = {"platform", "cohort", "source_revision", "built_at", "verification", "instructions", "artifacts"}
    if set(specification) != keys:
        fail("Unknown or missing publication manifest fields")
    platform = specification.get("platform")
    if platform not in PLATFORMS:
        fail("Unknown debug platform")
    cohort = text_field(specification, "cohort")
    revision = text_field(specification, "source_revision")
    if not re.fullmatch(r"[a-fA-F0-9]{40}", revision):
        fail("source_revision must identify the full declared source commit")
    built_at = text_field(specification, "built_at")
    parsed = dt.datetime.fromisoformat(built_at.replace("Z", "+00:00"))
    if parsed.tzinfo is None:
        fail("built_at must include its time zone")
    instructions = text_field(specification, "instructions")
    verification = specification.get("verification")
    if not isinstance(verification, dict) or verification.get("status") != "passed":
        fail("Only already verified packages may be published")
    if verification.get("level") not in {"static", "runtime"} or verification.get("human_review") not in {"pending", "approved"}:
        fail("Verification level and human-review state must be explicit")
    text_field(verification, "summary")
    text_field(verification, "reference")
    if verification["human_review"] == "approved":
        text_field(verification, "human_review_note")
    artifacts = specification.get("artifacts")
    if not isinstance(artifacts, list) or not 1 <= len(artifacts) <= 16:
        fail("Provide one to sixteen verified package files")
    sources = []
    seen = set()
    for artifact in artifacts:
        if not isinstance(artifact, dict):
            fail("Invalid artifact entry")
        relative = artifact.get("path")
        if not isinstance(relative, str):
            fail("Artifact path is missing")
        path = Path(relative)
        if path.is_absolute() or ".." in path.parts or not path.parts or path.parts[0] != "build":
            fail("Artifact sources must be declared inside this project's build directory")
        name = filename(path.name, artifact=True)
        expected = artifact.get("sha256", "")
        if not isinstance(expected, str) or not re.fullmatch(r"[0-9a-f]{64}", expected):
            fail("Each artifact needs the SHA256 already recorded by its verification")
        if name in seen or name in {META, "README.md"}:
            fail("Package filenames must be unique")
        seen.add(name)
        source = repo / path
        plain(source)
        sources.append((source, name, expected))
    build = repo / "build"
    plain(build, directory=True)
    release = build / "releases"
    make_directory(release)
    debug = release / "debug"
    make_directory(debug)
    make_directory(release / "public")  # No files are ever published to public by this tool.
    current = debug / platform
    plain(current, directory=True, missing=True)
    lock = debug / f".{platform}-publish.lock"
    plain(lock, missing=True)
    descriptor = os.open(lock, os.O_CREAT | os.O_EXCL | os.O_WRONLY, 0o600)
    os.close(descriptor)
    staged = None
    cleanup_owned = True
    try:
        previous = inventory(current, platform) if current.exists() else None
        staged = Path(tempfile.mkdtemp(prefix=f".{platform}-prepare-", dir=debug))
        managed = {}
        for source, name, expected in sources:
            target = staged / name
            shutil.copyfile(source, target)
            os.chmod(target, 0o644)
            actual = digest(target)
            if actual != expected:
                fail(f"Artifact differs from the verified bytes: {name}")
            managed[name] = {"bytes": target.stat().st_size, "sha256": actual}
        human = "pending" if verification["human_review"] == "pending" else "approved: " + verification["human_review_note"]
        level = "package inspection, without running the game" if verification["level"] == "static" else "game execution"
        readme = (f"# OpenDollhouse: {platform} test build\n\nBuild group: {cohort}\n\nPrepared: {built_at}\n\n"
                  f"Verification: {level} — {verification['summary']}\n\n"
                  f"Human review: {human}.\n\n{instructions}\n\n"
                  "This is a debug package for testing, not a public release.\n"
                  "Players supply their own commercial game data separately.\n")
        (staged / "README.md").write_text(readme, encoding="utf-8")
        managed["README.md"] = {"bytes": (staged / "README.md").stat().st_size, "sha256": digest(staged / "README.md")}
        metadata = {"schema": 1, "publisher": OWNER, "channel": "debug", "platform": platform,
                    "cohort": cohort, "source_revision": revision, "built_at": built_at,
                    "published_at": dt.datetime.now(dt.timezone.utc).isoformat(),
                    "verification": verification, "instructions": instructions, "managed_files": managed}
        (staged / META).write_text(json.dumps(metadata, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
        # Recheck immediately before the atomic exchange, including modified or unknown files.
        if previous is not None:
            if inventory(current, platform) != previous:
                fail("Current metadata changed during publication")
            exchange(staged, current)
            cleanup_owned = False  # It now contains the previously published package.
            # An unexpected concurrent opener/editor keeps the old package intact.
            try:
                if inventory(staged, platform) != previous:
                    fail("Previous metadata changed during the exchange")
            except Exception:
                exchange(staged, current)
                # The new package was briefly visible at current. A concurrent
                # opener/editor can therefore have acquired it before rollback.
                # Only a fresh complete check permits removing that directory.
                try:
                    cleanup_owned = inventory(staged, platform) == metadata
                except Exception:
                    cleanup_owned = False
                raise
            cleanup_owned = True  # Only unchanged, verified managed files remain.
        else:
            if current.exists() or current.is_symlink():
                fail("Destination appeared during publication")
            os.rename(staged, current)
            staged = None
        return current
    finally:
        if staged is not None:
            if cleanup_owned:
                shutil.rmtree(staged)
            else:
                print(f"Preserved publication directory for inspection: {staged}", file=sys.stderr)
        lock.unlink()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("manifest", type=Path, help="Explicit verified debug publication manifest (JSON)")
    args = parser.parse_args()
    try:
        repo = Path(__file__).resolve().parents[1]
        result = publish(repo, read_json(args.manifest.absolute()))
        print(result)
        return 0
    except (OSError, ValueError, TypeError, KeyError, json.JSONDecodeError) as error:
        print(f"Debug publication stopped: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
