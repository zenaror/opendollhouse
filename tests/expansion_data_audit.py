"""Read-only catalog/data coverage report; never evidence of playable coverage."""
import argparse
import json
import subprocess
from pathlib import Path


def catalog(binary: Path, user_data: Path, mode: int) -> dict:
    result = subprocess.run([str(binary), "--user-data", str(user_data), "--mode", str(mode)], check=False, capture_output=True, text=True)
    if result.returncode:
        raise RuntimeError(f"odh-neighborhood mode {mode} failed: {result.stderr.strip()}")
    try:
        payload = json.loads(result.stdout)
    except json.JSONDecodeError as exc:
        raise RuntimeError(f"odh-neighborhood mode {mode} returned invalid JSON") from exc
    if payload.get("evidence") != "catalog_selection_only":
        raise RuntimeError(f"odh-neighborhood mode {mode} returned unexpected evidence label")
    return payload


def build_report(binary: Path, installation: Path, user_data: Path) -> dict:
    modes = []
    for mode in range(1, 8):
        data = catalog(binary, user_data, mode)
        lots = data.get("lots", [])
        families = [lot["family"] for lot in lots if lot.get("family") is not None]
        modes.append({
            "mode": mode,
            "status": data.get("status", "unknown"),
            "catalog_lot_rows": len(lots),
            "house_files_present": sum(bool(lot.get("file_exists")) for lot in lots),
            "missing_referenced_house_files": [lot["path"] for lot in lots if not lot.get("file_exists", False)],
            "fami_associations": len(families),
            "fami_member_entries": sum(family.get("member_count", 0) for family in families),
        })
    templates = []
    if installation.is_dir():
        for folder in sorted((p for p in installation.rglob("Template*") if p.is_dir()), key=lambda p: p.as_posix().casefold()):
            templates.append({
                "directory": folder.relative_to(installation).as_posix(),
                "iff_file_count": sum(1 for p in folder.rglob("*") if p.is_file() and p.suffix.casefold() == ".iff"),
            })
        far_count = sum(1 for p in installation.rglob("*") if p.is_file() and p.suffix.casefold() == ".far")
    else:
        far_count = 0
    return {
        "evidence": "data_catalog_coverage_only", "playable_coverage": False,
        "installation_root_exists": installation.is_dir(), "user_data_exists": user_data.is_dir(),
        "modes": modes, "expansion_template_directories": templates, "far_archive_count": far_count,
        "note": "Presence and catalog counts do not prove resource validity, runtime support, or playable coverage.",
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True, help="path to odh-neighborhood")
    parser.add_argument("--installation", type=Path, required=True, help="game installation root; scanned read-only for Template* folders and FAR count")
    parser.add_argument("--user-data", type=Path, help="UserData directory; defaults to INSTALLATION/UserData")
    args = parser.parse_args()
    user_data = args.user_data or args.installation / "UserData"
    print(json.dumps(build_report(args.binary, args.installation, user_data), indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
