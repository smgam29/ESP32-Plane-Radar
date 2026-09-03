#!/usr/bin/env python3
"""Build a lean, self-checking first-upgrade ZIP (not a merged factory image)."""
import argparse
import hashlib
import json
import shutil
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO))
from installer.upgrade import PARTS, FLASH_SIZE, validate_package


def build(version, build_dir, guide, output_dir):
    if not guide.is_file() or guide.read_bytes()[:5] != b"%PDF-":
        raise ValueError("A rendered A4 PDF guide is required.")
    output_dir.mkdir(parents=True, exist_ok=True)
    name = "Plane-Radar-Upgrade-" + version
    if any(c not in "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.-" for c in version):
        raise ValueError("Invalid version string.")
    with tempfile.TemporaryDirectory(prefix="plane-radar-package-") as temporary:
        root = Path(temporary) / name
        (root / "firmware").mkdir(parents=True)
        manifest = {"name": "Plane Radar custom firmware", "version": version,
                    "chip": "ESP32-C3", "flash_size": FLASH_SIZE,
                    "source_commit": subprocess.check_output(
                        ["git", "rev-parse", "HEAD"], cwd=REPO, text=True).strip(),
                    "parts": {}}
        for part, offset in PARTS.items():
            data = (build_dir / part).read_bytes()
            (root / "firmware" / part).write_bytes(data)
            manifest["parts"][part] = {"offset": offset, "size": len(data),
                "sha256": hashlib.sha256(data).hexdigest()}
        (root / "release.json").write_text(json.dumps(manifest, indent=2) + "\n")
        for file in ("upgrade.py", "requirements.txt", "Start-Mac.command",
                     "Start-Windows.cmd", "START-HERE.txt"):
            shutil.copyfile(REPO / "installer" / file, root / file)
        shutil.copyfile(REPO / "LICENSE", root / "LICENSE.txt")
        shutil.copyfile(guide, root / "Upgrade-Guide-A4.pdf")
        validate_package(root)
        checksums = []
        for file in sorted(root.rglob("*")):
            if file.is_file():
                checksums.append(hashlib.sha256(file.read_bytes()).hexdigest() +
                                 "  " + file.relative_to(root).as_posix())
        (root / "SHA256SUMS.txt").write_text("\n".join(checksums) + "\n")
        archive = output_dir / (name + ".zip")
        with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as package:
            for file in sorted(root.rglob("*")):
                if file.is_file():
                    package.write(file, file.relative_to(root.parent))
        # Read-back verification catches packaging mistakes as well as CRC errors.
        with zipfile.ZipFile(archive) as package:
            if package.testzip():
                raise ValueError("ZIP CRC verification failed")
            for line in checksums:
                digest, relative = line.split("  ", 1)
                if hashlib.sha256(package.read(name + "/" + relative)).hexdigest() != digest:
                    raise ValueError("ZIP content checksum failed: " + relative)
        digest = hashlib.sha256(archive.read_bytes()).hexdigest()
        archive.with_suffix(".zip.sha256").write_text(digest + "  " + archive.name + "\n")
    print(f"Verified {archive} ({archive.stat().st_size:,} bytes)")
    return archive


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--version", required=True)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--guide", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, default=Path("dist"))
    args = parser.parse_args()
    build(args.version, args.build_dir, args.guide, args.output_dir)
