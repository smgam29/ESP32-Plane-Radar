#!/usr/bin/env python3
"""Build a small unsigned macOS app/DMG around the guarded USB upgrader."""
import argparse
import hashlib
import json
import os
import plistlib
import shutil
import subprocess
import tempfile
import zipfile
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]


def build(package_root: Path, output_dir: Path):
    manifest = json.loads((package_root / "release.json").read_text())
    version = manifest["version"]
    safe = "".join(c for c in version if c.isalnum() or c in ".-")
    if safe != version or not version:
        raise ValueError("Invalid package version")
    output_dir.mkdir(parents=True, exist_ok=True)
    base = f"Plane-Radar-Mac-Installer-{version}"
    with tempfile.TemporaryDirectory(prefix="plane-radar-mac-") as temporary:
        app = Path(temporary) / "Plane Radar Installer.app"
        macos = app / "Contents/MacOS"
        resources = app / "Contents/Resources"
        macos.mkdir(parents=True)
        resources.mkdir()
        launcher = macos / "Plane Radar Installer"
        shutil.copyfile(REPO / "installer/macos-launcher.sh", launcher)
        launcher.chmod(0o755)
        shutil.copytree(package_root, resources / "package")
        plist = {
            "CFBundleName": "Plane Radar Installer",
            "CFBundleDisplayName": "Plane Radar Installer",
            "CFBundleIdentifier": "uk.co.planeradar.installer",
            "CFBundleVersion": version,
            "CFBundleShortVersionString": version,
            "CFBundlePackageType": "APPL",
            "CFBundleExecutable": "Plane Radar Installer",
            "LSMinimumSystemVersion": "10.15",
            "NSHighResolutionCapable": True,
        }
        with (app / "Contents/Info.plist").open("wb") as stream:
            plistlib.dump(plist, stream, sort_keys=False)

        archive = output_dir / f"{base}.app.zip"
        with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as zf:
            for path in sorted(app.rglob("*")):
                if not path.is_file():
                    continue
                info = zipfile.ZipInfo.from_file(path, path.relative_to(app.parent))
                info.external_attr = (0o755 if os.access(path, os.X_OK) else 0o644) << 16
                with path.open("rb") as stream:
                    zf.writestr(info, stream.read(), compress_type=zipfile.ZIP_DEFLATED,
                                compresslevel=9)
        if zipfile.ZipFile(archive).testzip():
            raise ValueError("macOS app ZIP verification failed")

        dmg = output_dir / f"{base}.dmg"
        if shutil.which("hdiutil"):
            dmg_root = Path(temporary) / "dmg"
            dmg_root.mkdir()
            shutil.copytree(app, dmg_root / app.name)
            shutil.copyfile(package_root / "Upgrade-Guide-A4.pdf",
                            dmg_root / "Upgrade Guide.pdf")
            shutil.copyfile(package_root / "User-Manual-A4.pdf",
                            dmg_root / "User Manual.pdf")
            shutil.copyfile(package_root / "START-HERE.txt",
                            dmg_root / "START HERE.txt")
            subprocess.run(["hdiutil", "create", "-quiet", "-volname",
                            "Plane Radar Installer", "-srcfolder", str(dmg_root),
                            "-format", "UDZO", "-ov", str(dmg)], check=True)
        for artifact in (archive, dmg):
            if artifact.exists():
                digest = hashlib.sha256(artifact.read_bytes()).hexdigest()
                artifact.with_suffix(artifact.suffix + ".sha256").write_text(
                    f"{digest}  {artifact.name}\n")
                print(f"Verified {artifact} ({artifact.stat().st_size:,} bytes)")
    return archive


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--package-root", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, default=Path("dist"))
    args = parser.parse_args()
    build(args.package_root, args.output_dir)
