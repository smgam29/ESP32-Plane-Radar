#!/usr/bin/env python3
"""Generate the printable, two-page A4 first-upgrade guide (ReportLab)."""
import argparse
from pathlib import Path
from xml.sax.saxutils import escape
from reportlab.lib import colors
from reportlab.lib.enums import TA_LEFT
from reportlab.lib.pagesizes import A4
from reportlab.lib.styles import ParagraphStyle
from reportlab.platypus import (SimpleDocTemplate, Paragraph, Spacer, Table,
                               TableStyle, PageBreak, KeepTogether)

NAVY = colors.HexColor("#163748")
TEAL = colors.HexColor("#087F79")
INK = colors.HexColor("#213742")
MUTED = colors.HexColor("#556974")
PALE = colors.HexColor("#EDF5F4")
WARM = colors.HexColor("#FFF3DF")
WIDTH = A4[0] - 80
BODY = ParagraphStyle("body", fontName="Helvetica", fontSize=10, leading=13.7,
                      textColor=INK, spaceAfter=5)
SMALL = ParagraphStyle("small", parent=BODY, fontSize=8.5, leading=11, textColor=MUTED)
TITLE = ParagraphStyle("title", parent=BODY, fontName="Helvetica-Bold", fontSize=26,
                       leading=29, textColor=NAVY, spaceAfter=9)
HEAD = ParagraphStyle("head", parent=BODY, fontName="Helvetica-Bold", fontSize=12,
                      leading=15, textColor=TEAL, spaceBefore=9, spaceAfter=5)
CODE = ParagraphStyle("code", parent=BODY, fontName="Courier", fontSize=8.4, leading=12,
                      spaceAfter=0)


def p(text, style=BODY):
    return Paragraph(text, style)


def box(text, color=PALE):
    block = Table([[p(text)]], colWidths=[WIDTH])
    block.setStyle(TableStyle([("BACKGROUND", (0, 0), (-1, -1), color),
        ("BOX", (0, 0), (-1, -1), .5, colors.HexColor("#CDDDDC")),
        ("LEFTPADDING", (0, 0), (-1, -1), 11), ("RIGHTPADDING", (0, 0), (-1, -1), 11),
        ("TOPPADDING", (0, 0), (-1, -1), 8), ("BOTTOMPADDING", (0, 0), (-1, -1), 5)]))
    return block


def command(text):
    block = Table([[p(escape(text), CODE)]], colWidths=[WIDTH])
    block.setStyle(TableStyle([("BACKGROUND", (0, 0), (-1, -1), PALE),
        ("LEFTPADDING", (0, 0), (-1, -1), 10), ("TOPPADDING", (0, 0), (-1, -1), 7),
        ("BOTTOMPADDING", (0, 0), (-1, -1), 7)]))
    return block


def build(version, output):
    output.parent.mkdir(parents=True, exist_ok=True)
    version = escape(version)
    def page(canvas, doc):
        canvas.saveState()
        canvas.setStrokeColor(TEAL)
        canvas.setLineWidth(2)
        canvas.line(40, A4[1] - 35, A4[0] - 40, A4[1] - 35)
        canvas.setFont("Helvetica-Bold", 8)
        canvas.setFillColor(NAVY)
        canvas.drawString(40, A4[1] - 27, "PLANE RADAR  /  CUSTOM FIRMWARE")
        canvas.setFont("Helvetica", 8)
        canvas.setFillColor(MUTED)
        canvas.drawString(40, 27, "USB first upgrade  |  " + version + "  |  Keep this guide with your backup")
        canvas.drawRightString(A4[0] - 40, 27, str(doc.page) + " / 2")
        canvas.restoreState()

    story = [p("Installer help (fallback)", TITLE),
        p("Use this only if the Mac installer app does not start or stops", HEAD),
        p(f"Package: <b>{version}</b> (tester build). For the <b>ESP32-C3 Super Mini, 4 MB flash and GC9A01 240 x 240 display</b>. Not an official MatixYo release."),
        box("<b>Start with Plane Radar Installer.app.</b> You do not need to follow this document first. Return here only for manual-launch instructions, explanations or recovery help.", colors.HexColor("#E7F8EC")),
        p("1  Prepare the computer", HEAD),
        p("Extract the <b>entire ZIP</b> into a writable folder. Keep all files together. Install <b>Python 3.11</b> from <link href='https://www.python.org/downloads/' color='#087F79'>python.org/downloads</link> if needed. The first launcher run needs internet to install a small, pinned USB flashing tool in this folder."),
        p("Use a known <b>USB data cable</b> and stable power. Close serial monitors and browser flashers; disconnect other ESP boards. Allow 5-15 minutes. Your radar still uses <b>2.4 GHz Wi-Fi</b>. No PlatformIO or source-code build is needed."),
        p("2  Enter USB download mode", HEAD),
        p("Unplug the radar. <b>Hold its range/BOOT button, plug in USB while holding it, then release.</b> The screen may stay blank; that is normal in download mode. This sequence uses the same BOOT button that normally changes range."),
        box("<b>Important:</b> do not hold BOOT for several seconds while the normal radar firmware is running - that resets its saved settings. If the case prevents access, stop rather than forcing it open.", WARM),
        p("3  Start the installer", HEAD),
        p("<b>macOS - easiest:</b> open the Mac Installer DMG. Because this tester app is not yet Apple-notarized, <b>Control-click Plane Radar Installer, choose Open, then Open</b>. It copies its working files to Documents / Plane Radar Installer and opens Terminal. Read that window; do not close it while flashing."),
        p("<b>macOS ZIP fallback:</b> open Terminal in the extracted folder (or type <b>cd</b>, a space, drag the folder into Terminal, then press Enter). Run:"),
        command("sh Start-Mac.command"),
        p("<b>Windows:</b> double-click <b>Start-Windows.cmd</b> in the extracted folder. Read the terminal window; do not close it during flashing."),
        p("Choose the radar's USB serial port from the numbered list. Windows usually shows COM followed by a number; macOS usually shows /dev/cu.usbmodem... .", SMALL),
        p("4  Back up, confirm, then leave it connected", HEAD),
        p("The tool checks the chip and 4 MB flash, reads a <b>complete private backup</b>, and checks the existing partition layout. An animated ASCII symbol and elapsed time show that long USB stages are active. Only then does it ask: <b>Continue? [y/n]</b>. Press <b>y</b> to install or <b>n</b> to stop safely."),
        p("Wait for <b>SUCCESS: firmware verified; NVS settings are byte-for-byte unchanged.</b> The installer writes only four specified firmware areas - not the NVS area holding Wi-Fi and radar preferences."),
        box("<b>Do not unplug during writing.</b> This first USB layout migration is not power-fail atomic. A failure may need USB recovery from the backup. Never choose erase-all or substitute a merged/factory image.", WARM),
        PageBreak(),
        p("Check, keep, update", TITLE),
        p("5  Reboot and confirm the result", HEAD),
        p("After SUCCESS, unplug USB, make sure BOOT is released, then reconnect normally. On a phone or computer on the same local Wi-Fi, open <link href='http://plane-radar.local' color='#087F79'><b>http://plane-radar.local</b></link>. If that fails, find the radar's IP in your router and open it directly. Use individual IPs if you own several radars."),
        p(f"Check the page reports <b>{version}</b>, the location is correct, aircraft update, and a short button press still changes range. Existing settings should be retained. If Wi-Fi setup opens, join <b>PlaneRadar-Setup</b> and browse to <b>http://192.168.4.1</b> to configure your 2.4 GHz network."),
        p("6  Keep your backup private", HEAD),
        p("Keep the generated <b>backups / device-and-time / original-4mb.bin</b> and its adjacent <b>original-4mb.json</b> together. Copy that folder to somewhere safe before deleting the upgrade package."),
        box("<b>The backup contains your Wi-Fi credentials.</b> Do not send it with bug reports, email it, or upload it to GitHub. It belongs to that physical radar; the recovery tool refuses a different device."),
        p("Next time: update through the web page", HEAD),
        p("Download a newer <b>*-ota.bin</b> from the <link href='https://github.com/smgam29/ESP32-Plane-Radar/releases' color='#087F79'>custom fork's GitHub Releases</link>. Open your radar's page, go to <b>Firmware</b>, choose that application file and press <b>Install update</b>. Leave power on until it succeeds and reboots; then refresh the page and check the version."),
        p("Do not use a <b>factory, merged, bootloader, partition or backup</b> file in the web updater. Incomplete later OTA uploads leave the active application in place; this is not a guarantee that every new firmware build is bug-free."),
        p("If something goes wrong", HEAD),
        p("<b>No port or failed connection:</b> try another data cable or USB port, close any serial monitor, and repeat the unplug/hold BOOT/connect/release sequence. Re-select the port if it changed. If Python is missing, install it and restart the launcher."),
        p("<b>Hardware, security or layout check rejected:</b> stop and contact the maintainer with the error text. Do not bypass checks or erase flash. The package accepts the project's known original single-slot layout and this fork's dual-slot layout only."),
        p("<b>Interrupted write or firmware does not boot:</b> put the <b>same radar</b> back in download mode. From the extracted folder, use the matching command below. Replace <b>DEVICE-TIME</b> with your actual backup folder name:"),
        p("macOS", SMALL),
        command('sh Start-Mac.command --restore "backups/DEVICE-TIME/original-4mb.bin"'),
        Spacer(1, 5), p("Windows - open Command Prompt in the extracted folder", SMALL),
        command('Start-Windows.cmd --restore "backups/DEVICE-TIME/original-4mb.bin"'),
        p("Recovery checks the backup hash and device identity, then asks <b>[y/n]</b>. Choosing y replaces all flash, including the original settings. After verification, unplug and reconnect with BOOT released.", SMALL),
        p("Sources & support", HEAD),
        p("<link href='https://github.com/smgam29/ESP32-Plane-Radar' color='#087F79'>Custom firmware and issues</link> &nbsp; / &nbsp; <link href='https://github.com/MatixYo/ESP32-Plane-Radar' color='#087F79'>Original project by MatixYo</link> &nbsp; / &nbsp; <link href='https://docs.espressif.com/projects/esptool/en/latest/esp32c3/' color='#087F79'>Espressif USB flashing documentation</link>", SMALL),
    ]
    document = SimpleDocTemplate(str(output), pagesize=A4, rightMargin=40, leftMargin=40,
        topMargin=51, bottomMargin=45, title="Plane Radar - Original Firmware Upgrade Guide",
        author="Plane Radar custom firmware project")
    document.build(story, onFirstPage=page, onLaterPages=page)
    if document.page != 2:
        raise ValueError(f"Guide overflowed: expected two A4 pages, got {document.page}")
    print(output)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--version", required=True)
    parser.add_argument("--output", type=Path, default=Path("output/pdf/Plane-Radar-Upgrade-Guide-A4.pdf"))
    args = parser.parse_args()
    build(args.version, args.output)
