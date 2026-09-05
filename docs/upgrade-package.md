# Original-firmware upgrade package

## Distribution design

One cross-platform ZIP plus a lightweight macOS app ZIP and DMG, containing four split images,
the guarded Python helper, Mac and Windows launchers, pinned esptool requirement,
release metadata, checksums, MIT attribution, quick-start text and a two-page A4
PDF. The Mac app copies its payload to a versioned folder under Documents and
opens the same interactive installer in Terminal, so backups remain writable and
easy to find. The launcher selects Python 3.9+ outside an obsolete activated Conda
environment, replaces stale Python 3.8 virtual environments, upgrades pip, and
requires binary dependencies rather than attempting local Rust/C compilation.
It is currently unsigned/unnotarized; first launch uses Control-click
then Open. No source build, PlatformIO, merged/factory image or private device data is
included. Python 3.11 is recommended. First launch downloads dependencies into
the extracted folder's `.venv`; the firmware and serial migration are local.

The installer deliberately stops before writing if it does not recognise the
hardware, security configuration, partition layout or image integrity. It is not
a universal ESP flasher. Checksums detect corruption, not a malicious distributor:
testers must obtain the ZIP from a trusted source.

## Supported migration

The supported stock layout is from MatixYo/ESP32-Plane-Radar, upstream commit
`cd7e60620e457f286d5db41dab5c10d45cbefe95`:

| Area | Offset | Original size | Custom size |
|---|---|---|---|
| NVS (preserved) | 0x9000 | 0x5000 | 0x5000 |
| OTA selection | 0xe000 | 0x2000 | 0x2000 |
| App 0 | 0x10000 | 0x300000 | 0x180000 |
| App 1 | 0x190000 | none | 0x180000 |
| SPIFFS (not written) | 0x310000 | 0xe0000 | 0xe0000 |
| Coredump (not written) | 0x3f0000 | 0x10000 | 0x10000 |

Known custom dual-slot layouts are also accepted for USB repair. Unknown layouts,
other flash sizes, and nonzero security/encryption flags are rejected. The helper
never uses `erase_flash`, `--erase-all`, or `--force`. It validates sector-rounded
write boundaries as well as byte lengths so no image can erase NVS.

Sequence: validate package -> identify chip/flash/security -> full 4 MB backup ->
validate existing table and MD5 -> explicit y/n confirmation -> split write -> verify
firmware -> reread and compare NVS -> user power-cycles normally. All flashing
commands leave the chip in download mode until validation is complete. A backup
and matching metadata must be retained privately; they contain Wi-Fi credentials.

The initial partition migration is not power-fail atomic. Recovery uses
`--restore` with that same device's full backup, checks its SHA-256 and MAC,
asks y/n, writes all flash after y, and verifies it. Incomplete writes never
overwrite the saved backup. Later OTA uses the two application slots; this does
not imply automatic runtime rollback for a logically broken firmware build.

## Rebuild

Use the same version embedded in the application binary (including a leading v
if the build uses one). The package validator rejects a version mismatch.

```sh
pio run -e supermini
pio run -e supermini -t merge
python3 -m pip install reportlab==4.4.9
python3 scripts/build_upgrade_guide.py --version 1.10.3-dev
python3 scripts/build_user_manual.py --version 1.10.3-dev
python3 scripts/build_upgrade_package.py --version 1.10.3-dev \
  --build-dir .pio/build/supermini \
  --guide output/pdf/Plane-Radar-Upgrade-Guide-A4.pdf \
  --manual output/pdf/Plane-Radar-User-Manual-A4.pdf --output-dir dist
python3 -m unittest discover -s test -p 'test_upgrade_installer.py' -v
```

The existing tag-release workflow builds these artifacts alongside the separate
OTA files. Building a local package does not publish a GitHub release. Render the
PDF after editing its text and inspect both pages; generated output is ignored
by Git. `--check-package` on the extracted helper is read-only and needs no radar.

## Verification for this delivery

- Actual C3 application image parsed by esptool 4.5.1: checksum and SHA valid.
- Every ZIP file read back and checked; firmware offset/size/hash guards passed.
- Real Mac shell launcher created its isolated environment and passed the
  extracted package's `--check-package`. No serial device was accessed.
- Unit tests simulate sector erasure, stock-to-dual migration, NVS preservation,
  restore, cancellation, wrong identity/security/layout, checksum corruption,
  oversized bootloader, interrupted write, and NVS verification failure.
- PDF confirmed as two A4 pages and both rendered pages visually inspected.

Not yet physically verified: this new helper's complete USB migration on an
original-firmware unit, Windows launcher execution, and recovery after a real
power interruption. Run the first stock-device migration as a supervised pilot;
do not describe simulated tests as hardware validation. Existing firmware has
been used on the project device, but that is separate evidence from installer
validation. No live device was reflashed to test this package.
