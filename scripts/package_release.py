#!/usr/bin/env python3
"""Package distinct OTA, factory, and NVS-preserving release artifacts."""

import argparse
import hashlib
import json
import shutil
import zipfile
from pathlib import Path


SPLIT_PARTS = (
    ("bootloader.bin", "0x0"),
    ("partitions.bin", "0x8000"),
    ("boot_app0.bin", "0xe000"),
    ("firmware.bin", "0x10000"),
)


def checksum(path: Path) -> None:
    digest = hashlib.sha256(path.read_bytes()).hexdigest()
    path.with_suffix(path.suffix + ".sha256").write_text(
        f"{digest}  {path.name}\n", encoding="ascii"
    )


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--version", required=True)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()

    args.output_dir.mkdir(parents=True, exist_ok=True)
    for filename, _ in SPLIT_PARTS:
        if not (args.build_dir / filename).is_file():
            raise FileNotFoundError(args.build_dir / filename)

    ota = args.output_dir / f"plane-radar-{args.version}-ota.bin"
    factory = args.output_dir / f"plane-radar-{args.version}-factory.bin"
    shutil.copyfile(args.build_dir / "firmware.bin", ota)
    shutil.copyfile(args.build_dir / "firmware-merged.bin", factory)

    installer_name = f"plane-radar-{args.version}-nvs-preserving"
    installer = args.output_dir / f"{installer_name}.zip"
    manifest = {
        "name": "Plane Radar",
        "version": args.version,
        "new_install_prompt_erase": False,
        "builds": [
            {
                "chipFamily": "ESP32-C3",
                "parts": [
                    {"path": filename, "offset": int(offset, 16)}
                    for filename, offset in SPLIT_PARTS
                ],
            }
        ],
    }
    instructions = """Plane Radar NVS-preserving first installation

Flash these four files without selecting an erase-all-flash option:
  bootloader.bin  @ 0x0
  partitions.bin  @ 0x8000
  boot_app0.bin   @ 0xe000
  firmware.bin    @ 0x10000

The NVS settings partition at 0x9000 is deliberately omitted. After this
one-time layout migration, install future *-ota.bin files from the device's
local web page. Never upload the *-factory.bin file through the OTA page.
"""
    with zipfile.ZipFile(installer, "w", zipfile.ZIP_DEFLATED) as archive:
        for filename, _ in SPLIT_PARTS:
            archive.write(args.build_dir / filename, filename)
        archive.writestr("manifest.json", json.dumps(manifest, indent=2) + "\n")
        archive.writestr("README.txt", instructions)

    for artifact in (ota, factory, installer):
        checksum(artifact)


if __name__ == "__main__":
    main()
