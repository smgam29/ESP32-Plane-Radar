"""No hardware writes: model stock flash and exercise the exact installer flow."""
import hashlib
import csv
import importlib.util
import json
import struct
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

REPO = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("upgrade", REPO / "installer/upgrade.py")
upgrade = importlib.util.module_from_spec(spec)
spec.loader.exec_module(upgrade)


def table(entries):
    data = b"".join(struct.pack("<HBBII16sI", 0x50AA, kind, sub, offset, size,
                    name.encode(), 0) for name, kind, sub, offset, size in entries)
    data += b"\xeb\xeb" + b"\xff" * 14 + hashlib.md5(data).digest()
    return data.ljust(0xC00, b"\xff")


class InstallerTests(unittest.TestCase):
    def test_full_backup_uses_reliable_baud_only(self):
        self.assertEqual(upgrade.operation_baud(
            ["read_flash", "--no-progress", "0", hex(upgrade.FLASH_SIZE), "backup.bin"]),
            "115200")
        self.assertEqual(upgrade.operation_baud(
            ["read_flash", hex(upgrade.NVS_START), hex(upgrade.NVS_SIZE), "nvs.bin"]),
            "460800")
        self.assertEqual(upgrade.operation_baud(["write_flash"]), "460800")

    def test_layouts_match_authoritative_csvs(self):
        kinds = {"data": 1, "app": 0}
        subtypes = {"nvs": 2, "ota": 0, "ota_0": 0x10, "ota_1": 0x11,
                    "spiffs": 0x82, "coredump": 3}
        for path, expected in [(REPO / "test/fixtures/original_partition.csv", upgrade.STOCK),
                               (REPO / "partitions/plane_radar.csv", upgrade.CUSTOM)]:
            rows = []
            for row in csv.reader(line for line in path.read_text().splitlines() if line.strip() and not line.startswith("#")):
                name, kind, subtype, offset, size = (x.strip() for x in row[:5])
                rows.append((name, kinds[kind], subtypes[subtype], int(offset, 0), int(size, 0)))
            self.assertEqual(expected, rows)

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.flash = bytearray(b"\xff" * upgrade.FLASH_SIZE)
        self.flash[0x8000:0x8C00] = table(upgrade.STOCK)
        self.flash[0x9000:0xE000] = bytes(range(256)) * 80
        self.original = bytes(self.flash)
        self.calls = []
        (self.root / "firmware").mkdir()
        version = "1.10.2-dev"
        image = bytearray(512)
        image[0] = 0xE9
        image[12:14] = (5).to_bytes(2, "little")
        image[64:64 + len(version) + 1] = (version + "\0").encode()
        blobs = {"bootloader.bin": bytes(image), "firmware.bin": bytes(image),
                 "partitions.bin": table(upgrade.CUSTOM), "boot_app0.bin": b"\xff" * 8192}
        self.manifest = {"version": version, "chip": "ESP32-C3", "flash_size": upgrade.FLASH_SIZE,
                         "parts": {}}
        for name, data in blobs.items():
            (self.root / "firmware" / name).write_bytes(data)
            self.manifest["parts"][name] = {"offset": upgrade.PARTS[name],
                "size": len(data), "sha256": upgrade.sha(data)}
        self.write_manifest()

    def write_manifest(self):
        (self.root / "release.json").write_text(json.dumps(self.manifest))

    def tool(self, port, args, first=False):
        self.calls.append(args)
        command = args[0]
        if command == "flash_id":
            return "Chip is ESP32-C3\nMAC: e0:72:a1:22:25:e0\nDetected flash size: 4MB"
        if command == "get_security_info":
            return "Flags: 0x00000000\nFlash_Crypt_Cnt: 0x0"
        if command == "read_flash":
            values = [arg for arg in args[1:] if arg != "--no-progress"]
            start, length = int(values[0], 0), int(values[1], 0)
            Path(values[2]).write_bytes(self.flash[start:start + length])
        elif command == "write_flash":
            self.assertNotIn("erase_flash", args)
            self.assertNotIn("--force", args)
            for i in range(7, len(args), 2):
                start, data = int(args[i], 0), Path(args[i + 1]).read_bytes()
                end = (start + len(data) + 4095) & ~4095
                self.flash[start:end] = b"\xff" * (end - start)
                self.flash[start:start + len(data)] = data
        elif command == "verify_flash":
            for i in range(1, len(args), 2):
                start, data = int(args[i], 0), Path(args[i + 1]).read_bytes()
                self.assertEqual(self.flash[start:start + len(data)], data)
        return "OK"

    def run_upgrade(self, answer="y"):
        with patch.object(upgrade, "ROOT", self.root), patch.object(upgrade, "tool", self.tool), patch("builtins.input", return_value=answer):
            upgrade.upgrade("TEST-PORT", upgrade.validate_package(self.root))

    def test_complete_stock_migration_and_restore(self):
        self.run_upgrade()
        self.assertEqual(self.flash[0x9000:0xE000], self.original[0x9000:0xE000])
        self.assertEqual(upgrade.validate_layout(self.flash), "custom dual-slot")
        backup = next(self.root.glob("backups/*/original-4mb.bin"))
        self.assertEqual(backup.read_bytes(), self.original)
        with patch.object(upgrade, "tool", self.tool), patch("builtins.input", return_value="y"):
            upgrade.restore("TEST-PORT", backup)
        self.assertEqual(self.flash, self.original)

    def test_cancel_never_writes(self):
        self.run_upgrade("n")
        self.assertEqual(self.flash, self.original)
        self.assertFalse(any(c[0] == "write_flash" for c in self.calls))

    def test_unknown_layout_never_writes(self):
        layout = upgrade.STOCK.copy()
        layout[0] = ("nvs", 1, 2, 0x9000, 0x4000)
        self.flash[0x8000:0x8C00] = table(layout)
        with self.assertRaises(ValueError):
            self.run_upgrade()
        self.assertFalse(any(c[0] == "write_flash" for c in self.calls))

    def test_corrupt_or_wrong_chip_package(self):
        image = self.root / "firmware/firmware.bin"
        data = bytearray(image.read_bytes()); data[12] = 0
        image.write_bytes(data)
        with self.assertRaises(ValueError):
            upgrade.validate_package(self.root)
        self.manifest["parts"]["firmware.bin"]["sha256"] = upgrade.sha(data)
        self.write_manifest()
        with self.assertRaises(ValueError):
            upgrade.validate_package(self.root)

    def test_bad_partition_checksum(self):
        self.flash[0x8005] ^= 1
        with self.assertRaises(ValueError):
            upgrade.validate_layout(self.flash)

    def test_restore_wrong_device_or_corrupt_backup(self):
        self.run_upgrade()
        backup = next(self.root.glob("backups/*/original-4mb.bin"))
        meta = json.loads(backup.with_suffix(".json").read_text())
        meta["mac"] = "00:00:00:00:00:00"
        backup.with_suffix(".json").write_text(json.dumps(meta))
        with patch.object(upgrade, "tool", self.tool), self.assertRaises(ValueError):
            upgrade.restore("TEST-PORT", backup)
        backup.write_bytes(b"corrupt")
        with self.assertRaises(ValueError):
            upgrade.restore("TEST-PORT", backup)

    def test_security_and_flash_size_rejected(self):
        for text in ("Detected flash size: 8MB", "Detected flash size: 4MB\nMAC: e0:72:a1:22:25:e0"):
            with patch.object(upgrade, "tool", side_effect=[text, "Flags: 0x01\nFlash_Crypt_Cnt: 0x0"]), self.assertRaises(ValueError):
                upgrade.inspect_device("TEST-PORT")

    def test_partial_write_keeps_original_backup(self):
        def fail_write(port, args, first=False):
            if args[0] == "write_flash":
                self.flash[0:4096] = b"\xff" * 4096
                raise RuntimeError("Simulated interrupted USB write")
            return self.tool(port, args, first)
        with patch.object(upgrade, "ROOT", self.root), patch.object(upgrade, "tool", fail_write), patch("builtins.input", return_value="y"), self.assertRaises(RuntimeError):
            upgrade.upgrade("TEST-PORT", self.manifest)
        backup = next(self.root.glob("backups/*/original-4mb.bin"))
        self.assertEqual(backup.read_bytes(), self.original)
        self.assertEqual(self.flash[0x9000:0xE000], self.original[0x9000:0xE000])

    def test_oversize_bootloader_would_erase_nvs(self):
        data = (self.root / "firmware/bootloader.bin").read_bytes().ljust(0x9001, b"\0")
        (self.root / "firmware/bootloader.bin").write_bytes(data)
        self.manifest["parts"]["bootloader.bin"].update(size=len(data), sha256=upgrade.sha(data))
        self.write_manifest()
        with self.assertRaises(ValueError):
            upgrade.validate_package(self.root)

    def test_nvs_mismatch_is_reported(self):
        def corrupt_nvs(port, args, first=False):
            result = self.tool(port, args, first)
            if args[0] == "verify_flash":
                self.flash[0x9000] ^= 1
            return result
        with patch.object(upgrade, "ROOT", self.root), patch.object(upgrade, "tool", corrupt_nvs), patch("builtins.input", return_value="y"), self.assertRaisesRegex(RuntimeError, "NVS verification failed"):
            upgrade.upgrade("TEST-PORT", self.manifest)

    def test_confirmation_reprompts_until_y_or_n(self):
        with patch("builtins.input", side_effect=["", "maybe", "Y"]):
            self.assertTrue(upgrade.confirm("Continue?"))
        with patch("builtins.input", return_value="NO"):
            self.assertFalse(upgrade.confirm("Continue?"))


if __name__ == "__main__":
    unittest.main()
