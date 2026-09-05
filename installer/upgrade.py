#!/usr/bin/env python3
"""Guarded USB migration. Standard-library checks; esptool is invoked as a CLI."""
import argparse
import hashlib
import importlib.metadata
import json
import re
import struct
import subprocess
import sys
import tempfile
import time
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parent
FLASH_SIZE = 0x400000
NVS_START, NVS_SIZE = 0x9000, 0x5000
ESPTOOL_VERSION = "4.5.1"
PARTS = {"bootloader.bin": 0, "partitions.bin": 0x8000,
         "boot_app0.bin": 0xE000, "firmware.bin": 0x10000}
COMMON = [("nvs", 1, 2, 0x9000, 0x5000),
          ("otadata", 1, 0, 0xE000, 0x2000)]
TAIL = [("spiffs", 1, 0x82, 0x310000, 0xE0000),
        ("coredump", 1, 3, 0x3F0000, 0x10000)]
STOCK = COMMON + [("app0", 0, 0x10, 0x10000, 0x300000)] + TAIL
CUSTOM = COMMON + [("app0", 0, 0x10, 0x10000, 0x180000),
                   ("app1", 0, 0x11, 0x190000, 0x180000)] + TAIL


def sha(data):
    return hashlib.sha256(data).hexdigest()


def partition_table(data):
    """Validate entries and IDF's partition-table MD5 record before using offsets."""
    entries = []
    for offset in range(0, min(len(data), 0xC00), 32):
        record = data[offset:offset + 32]
        if len(record) != 32:
            break
        if record[:2] == b"\xeb\xeb":
            if record[16:] != hashlib.md5(data[:offset]).digest():
                raise ValueError("Partition-table MD5 mismatch; refusing to write.")
            return entries
        magic, kind, subtype, start, size, name, flags = struct.unpack("<HBBII16sI", record)
        if magic != 0x50AA or flags != 0:
            raise ValueError("Unsupported or encrypted partition table; refusing to write.")
        entries.append((name.split(b"\0")[0].decode("ascii"), kind, subtype, start, size))
    raise ValueError("No valid partition-table checksum; refusing to write.")


def validate_layout(flash):
    if len(flash) != FLASH_SIZE:
        raise ValueError("Backup must contain exactly 4 MB; refusing to write.")
    table = partition_table(flash[0x8000:0x9000])
    if table not in (STOCK, CUSTOM):
        raise ValueError("Unrecognised layout. Backup retained; no firmware written. Contact the maintainer.")
    return "original single-slot" if table == STOCK else "custom dual-slot"


def validate_package(root=ROOT):
    manifest = json.loads((root / "release.json").read_text())
    if manifest.get("chip") != "ESP32-C3" or manifest.get("flash_size") != FLASH_SIZE:
        raise ValueError("Wrong package hardware.")
    if set(manifest["parts"]) != set(PARTS):
        raise ValueError("Unexpected package file list.")
    limits = {"bootloader.bin": 0x8000, "partitions.bin": 0x9000,
              "boot_app0.bin": 0x10000, "firmware.bin": 0x190000}
    blobs = {}
    for name, start in PARTS.items():
        info = manifest["parts"][name]
        data = (root / "firmware" / name).read_bytes()
        if info["offset"] != start or len(data) != info["size"] or sha(data) != info["sha256"]:
            raise ValueError("Package checksum/size/offset mismatch: " + name)
        erase_end = (start + len(data) + 0xFFF) & ~0xFFF
        if not data or erase_end > limits[name]:
            raise ValueError("Image would exceed its safe flash area: " + name)
        if start < NVS_START + NVS_SIZE and erase_end > NVS_START:
            raise ValueError("Image would erase saved settings: " + name)
        if name in ("bootloader.bin", "firmware.bin"):
            if len(data) < 24 or data[0] != 0xE9 or int.from_bytes(data[12:14], "little") != 5:
                raise ValueError("Not an ESP32-C3 application/bootloader: " + name)
        blobs[name] = data
    if len(blobs["boot_app0.bin"]) != 0x2000:
        raise ValueError("Invalid OTA initialisation image.")
    if partition_table(blobs["partitions.bin"]) != CUSTOM:
        raise ValueError("Unexpected target partition layout.")
    if (manifest["version"] + "\0").encode() not in blobs["firmware.bin"]:
        raise ValueError("Firmware version does not match package metadata.")
    return manifest


def operation_baud(args):
    read_values = [arg for arg in args[1:] if arg != "--no-progress"]
    full_backup = (args[0] == "read_flash" and len(read_values) >= 2 and
                   read_values[0] == "0" and int(read_values[1], 0) == FLASH_SIZE)
    return "115200" if full_backup else "460800"


def tool(port, args, first=False):
    baud = operation_baud(args)
    full_backup = baud == "115200"
    command = [sys.executable, "-m", "esptool", "--chip", "esp32c3",
               "--port", port, "--baud", baud, "--before",
               "default_reset" if first else "no_reset", "--after", "no_reset"] + args
    labels = {"flash_id": "Connecting to the radar", "get_security_info": "Checking device security",
              "read_flash": "Reading and safeguarding flash", "write_flash": "Installing firmware",
              "verify_flash": "Verifying written firmware"}
    label = labels.get(args[0], "Communicating with the radar")
    if full_backup:
        label += " (safe speed; usually 5-7 minutes)"
    print(f"\n==> {label}", flush=True)
    frames = "|/-\\"
    started = time.monotonic()
    with tempfile.TemporaryFile(mode="w+t") as log:
        process = subprocess.Popen(command, text=True, stdout=log, stderr=subprocess.STDOUT)
        while process.poll() is None:
            elapsed = int(time.monotonic() - started)
            print(f"\r    [{frames[elapsed % len(frames)]}] Working... {elapsed}s  ", end="", flush=True)
            if elapsed >= 900:
                process.terminate()
                raise RuntimeError("USB operation timed out after 15 minutes.")
            time.sleep(0.25)
        log.seek(0)
        output = log.read()
    state = "OK" if process.returncode == 0 else "FAILED"
    print(f"\r    [{state}] {label} ({int(time.monotonic() - started)}s)          ", flush=True)
    if process.returncode:
        print(output, flush=True)
        raise RuntimeError("USB operation failed. Keep the backup. Reconnect in BOOT mode and retry; do not erase all flash.")
    return output


def inspect_device(port):
    output = tool(port, ["flash_id"], first=True)
    if "Detected flash size: 4MB" not in output:
        raise ValueError("This package requires exactly 4 MB flash.")
    found = re.search(r"MAC:\s*([0-9a-fA-F:]{17})", output)
    if not found:
        raise ValueError("Could not read device identity; refusing to write.")
    security = tool(port, ["get_security_info"])
    flags = re.search(r"Flags:\s*(0x[0-9a-fA-F]+)", security)
    crypt = re.search(r"Flash_Crypt_Cnt:\s*(0x[0-9a-fA-F]+)", security)
    if not flags or not crypt or int(flags[1], 16) or int(crypt[1], 16):
        raise ValueError("Security/encryption settings are not supported; refusing to write.")
    return found[1].lower()


def choose_port():
    from serial.tools.list_ports import comports
    ports = sorted(comports(), key=lambda p: p.device)
    if not ports:
        raise ValueError("No serial ports. Use a data cable and enter BOOT mode, then retry.")
    usb_ports = [port for port in ports if port.vid is not None]
    if len(usb_ports) == 1:
        print(f"Found radar USB port: {usb_ports[0].device} - {usb_ports[0].description}")
        return usb_ports[0].device
    print("Select the radar's USB port (disconnect other ESP boards first):")
    for i, port in enumerate(ports, 1):
        print(f"  {i}. {port.device} - {port.description}")
    index = int(input("Port number: ")) - 1
    if index < 0 or index >= len(ports):
        raise ValueError("Invalid port choice.")
    return ports[index].device


def confirm(question):
    while True:
        answer = input(question + " [y/n]: ").strip().lower()
        if answer in ("y", "yes"):
            return True
        if answer in ("n", "no"):
            return False
        print("Please enter y for yes or n for no.")


def flash_args(root):
    return [item for name, offset in PARTS.items()
            for item in (hex(offset), str(root / "firmware" / name))]


def backup_device(port, mac):
    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S%fZ")
    folder = ROOT / "backups" / (mac.replace(":", "") + "-" + stamp)
    folder.mkdir(parents=True, exist_ok=False, mode=0o700)
    image = folder / "original-4mb.bin"
    tool(port, ["read_flash", "--no-progress", "0", hex(FLASH_SIZE), str(image)])
    image.chmod(0o600)
    data = image.read_bytes()
    if len(data) != FLASH_SIZE:
        raise ValueError("Incomplete backup; no flash written.")
    metadata = {"mac": mac, "size": len(data), "sha256": sha(data),
                "nvs_sha256": sha(data[NVS_START:NVS_START + NVS_SIZE]), "created_utc": stamp}
    image.with_suffix(".json").write_text(json.dumps(metadata, indent=2) + "\n")
    print(f"PRIVATE BACKUP: {image}\nContains saved Wi-Fi credentials. Do not share it.")
    return image, data


def upgrade(port, manifest):
    print("\n========================================")
    print(" PLANE RADAR - ORIGINAL FIRMWARE UPGRADE")
    print("========================================")
    print("Keep the USB cable connected. Animated symbols show that each stage is active.")
    mac = inspect_device(port)
    backup, data = backup_device(port, mac)
    print("Detected layout:", validate_layout(data))
    print(f"Target: Plane Radar {manifest['version']} on {mac}")
    print("Writes bootloader, partition table, OTA data and app. NVS is excluded.")
    if not confirm("Ready to install the custom firmware. Continue?"):
        print("Cancelled. No flash written. Unplug and reconnect USB to run the existing firmware.")
        return
    parts = flash_args(ROOT)
    tool(port, ["write_flash", "--flash_mode", "keep", "--flash_freq", "keep",
                "--flash_size", "keep"] + parts)
    tool(port, ["verify_flash"] + parts)
    check = backup.parent / "nvs-after.bin"
    tool(port, ["read_flash", hex(NVS_START), hex(NVS_SIZE), str(check)])
    check.chmod(0o600)
    if check.read_bytes() != data[NVS_START:NVS_START + NVS_SIZE]:
        raise RuntimeError("NVS verification failed. Keep both backups and contact the maintainer before proceeding.")
    print("SUCCESS: firmware verified; NVS settings are byte-for-byte unchanged.")
    print("Unplug USB, release BOOT, and reconnect normally. Open http://plane-radar.local")
    print("Confirm the version and radar operation. Keep your private backup for recovery.")


def restore(port, image):
    data = image.read_bytes()
    metadata = json.loads(image.with_suffix(".json").read_text())
    if len(data) != FLASH_SIZE or sha(data) != metadata["sha256"] or metadata["size"] != FLASH_SIZE:
        raise ValueError("Backup checksum or length mismatch. Restore refused.")
    mac = inspect_device(port)
    if mac != metadata["mac"]:
        raise ValueError("Backup belongs to a different device. Restore refused.")
    print("This restores ALL flash, including the original Wi-Fi/radar settings.")
    if not confirm("Restore this device from the selected full backup?"):
        print("Cancelled. Unplug and reconnect normally.")
        return
    tool(port, ["write_flash", "--flash_mode", "keep", "--flash_freq", "keep",
                "--flash_size", "keep", "0", str(image)])
    tool(port, ["verify_flash", "0", str(image)])
    print("Restore verified. Unplug USB, release BOOT, then reconnect normally.")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check-package", action="store_true", help="Validate bundled files without accessing a device")
    parser.add_argument("--port", help="COM port or /dev/cu.*; otherwise choose interactively")
    parser.add_argument("--restore", type=Path, help="Restore this device's original-4mb.bin plus adjacent .json")
    args = parser.parse_args()
    if args.check_package:
        print("Package checks passed:", validate_package()["version"])
        return
    if importlib.metadata.version("esptool") != ESPTOOL_VERSION:
        raise ValueError("Use the included launcher to install esptool " + ESPTOOL_VERSION)
    manifest = None if args.restore else validate_package()
    port = args.port or choose_port()
    if args.restore:
        restore(port, args.restore.resolve())
    else:
        upgrade(port, manifest)


if __name__ == "__main__":
    try:
        main()
    except (Exception, KeyboardInterrupt) as error:
        print(f"\nSTOPPED: {error}\nNo erase-all command is used. If writing had begun, use the private backup to recover.", file=sys.stderr)
        sys.exit(1)
