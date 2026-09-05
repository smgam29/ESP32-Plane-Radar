import json
import tempfile
import unittest
import zipfile
from pathlib import Path
from unittest.mock import patch

from scripts import build_macos_installer


class MacInstallerTests(unittest.TestCase):
    def test_launcher_avoids_old_conda_python_and_source_builds(self):
        launcher = (build_macos_installer.REPO / "installer/Start-Mac.command").read_text()
        self.assertLess(launcher.index("/opt/homebrew/bin/python3"),
                        launcher.index("command -v python3"))
        self.assertIn("sys.version_info >= (3, 9)", launcher)
        self.assertIn("venv --clear", launcher)
        self.assertIn("--only-binary=cryptography,cffi", launcher)

    def test_app_contains_executable_launcher_and_complete_payload(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            package = root / "package"
            output = root / "output"
            package.mkdir()
            (package / "release.json").write_text(json.dumps({"version": "1.10.3-dev"}))
            (package / "upgrade.py").write_text("print('test')\n")
            (package / "Upgrade-Guide-A4.pdf").write_bytes(b"%PDF-test")
            (package / "User-Manual-A4.pdf").write_bytes(b"%PDF-test")
            (package / "START-HERE.txt").write_text("Start here")
            with patch.object(build_macos_installer.shutil, "which", return_value=None):
                archive = build_macos_installer.build(package, output)
            self.assertTrue(archive.is_file())
            self.assertTrue(archive.with_suffix(".zip.sha256").is_file())
            with zipfile.ZipFile(archive) as zf:
                launcher = next(i for i in zf.infolist() if i.filename.endswith("Contents/MacOS/Plane Radar Installer"))
                self.assertEqual((launcher.external_attr >> 16) & 0o111, 0o111)
                self.assertIn("Plane Radar Installer.app/Contents/Resources/package/release.json", zf.namelist())
                plist = zf.read("Plane Radar Installer.app/Contents/Info.plist")
                self.assertIn(b"uk.co.planeradar.installer", plist)


if __name__ == "__main__":
    unittest.main()
