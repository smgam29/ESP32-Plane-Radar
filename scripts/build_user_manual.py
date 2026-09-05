#!/usr/bin/env python3
"""Generate the A4 Plane Radar user manual."""
import argparse
import tempfile
from pathlib import Path
from reportlab.lib import colors
from reportlab.lib.pagesizes import A4
from reportlab.lib.styles import ParagraphStyle
from reportlab.pdfgen import canvas as pdfcanvas
from reportlab.graphics.barcode import qr
from reportlab.graphics.shapes import Drawing
from reportlab.platypus import SimpleDocTemplate, Paragraph, Table, TableStyle, PageBreak
from pypdf import PdfReader, PdfWriter

NAVY = colors.HexColor("#163748")
TEAL = colors.HexColor("#087F79")
INK = colors.HexColor("#213742")
MUTED = colors.HexColor("#556974")
PALE = colors.HexColor("#EDF5F4")
WARM = colors.HexColor("#FFF3DF")
WIDTH = A4[0] - 80
BODY = ParagraphStyle("body", fontName="Helvetica", fontSize=10, leading=14,
                      textColor=INK, spaceAfter=7)
SMALL = ParagraphStyle("small", parent=BODY, fontSize=8.5, leading=11, textColor=MUTED)
TITLE = ParagraphStyle("title", parent=BODY, fontName="Helvetica-Bold", fontSize=25,
                       leading=29, textColor=NAVY, spaceAfter=10)
HEAD = ParagraphStyle("head", parent=BODY, fontName="Helvetica-Bold", fontSize=13,
                      leading=16, textColor=TEAL, spaceBefore=8, spaceAfter=5)
SUB = ParagraphStyle("sub", parent=BODY, fontName="Helvetica-Bold", fontSize=10.5,
                     leading=14, textColor=NAVY, spaceBefore=4, spaceAfter=3)


def p(text, style=BODY): return Paragraph(text, style)


def box(text, color=PALE):
    table = Table([[p(text)]], colWidths=[WIDTH])
    table.setStyle(TableStyle([("BACKGROUND", (0, 0), (-1, -1), color),
        ("BOX", (0, 0), (-1, -1), .5, colors.HexColor("#CDDDDC")),
        ("LEFTPADDING", (0, 0), (-1, -1), 11), ("RIGHTPADDING", (0, 0), (-1, -1), 11),
        ("TOPPADDING", (0, 0), (-1, -1), 8), ("BOTTOMPADDING", (0, 0), (-1, -1), 4)]))
    return table


def rows(data, widths=(145, WIDTH - 145)):
    table = Table([[p(a, SUB), p(b)] for a, b in data], colWidths=widths, repeatRows=0)
    table.setStyle(TableStyle([("VALIGN", (0, 0), (-1, -1), "TOP"),
        ("GRID", (0, 0), (-1, -1), .35, colors.HexColor("#CDDDDC")),
        ("BACKGROUND", (0, 0), (0, -1), PALE),
        ("LEFTPADDING", (0, 0), (-1, -1), 8), ("RIGHTPADDING", (0, 0), (-1, -1), 8),
        ("TOPPADDING", (0, 0), (-1, -1), 5), ("BOTTOMPADDING", (0, 0), (-1, -1), 3)]))
    return table


def draw_quick_flyer(path, version):
    """Recreate the original safety-first one-page flyer as the manual cover."""
    c = pdfcanvas.Canvas(str(path), pagesize=A4)
    w, h = A4
    c.setFillColor(colors.HexColor("#081D39")); c.rect(0, h - 92, w, 92, fill=1, stroke=0)
    c.setFillColor(colors.white); c.setFont("Helvetica", 25); c.drawString(98, h - 43, "DESK PLANE RADAR")
    c.setFillColor(colors.HexColor("#52E879")); c.setFont("Helvetica", 12); c.drawString(99, h - 66, "QUICK SETUP GUIDE")
    c.setFillColor(colors.white); c.setFont("Helvetica", 8); c.drawString(99, h - 81, "Live aircraft around you, on a tiny round radar.")
    # Simple radar mark.
    c.setStrokeColor(colors.HexColor("#20C45A")); c.setLineWidth(1.5)
    for r in (10, 20, 30): c.circle(54, h - 47, r, stroke=1, fill=0)
    c.line(24, h - 47, 84, h - 47); c.line(54, h - 77, 54, h - 17)
    c.setLineWidth(4); c.line(54, h - 47, 70, h - 66)

    def card(x, y, cw, ch, title, text, fill="#FFFFFF", border="#D8E1E8", title_color="#073A63"):
        c.setFillColor(colors.HexColor(fill)); c.setStrokeColor(colors.HexColor(border)); c.setLineWidth(.8)
        c.roundRect(x, y, cw, ch, 10, fill=1, stroke=1)
        p(title, ParagraphStyle("ct", parent=HEAD, fontSize=11, leading=13,
          textColor=colors.HexColor(title_color), spaceBefore=0)).wrapOn(c, cw-26, ch)
        title_p = p(title, ParagraphStyle("ct2", parent=HEAD, fontSize=11, leading=13,
          textColor=colors.HexColor(title_color), spaceBefore=0))
        _, th = title_p.wrap(cw-26, ch); title_p.drawOn(c, x+13, y+ch-18-th)
        body_p = p(text, ParagraphStyle("cb", parent=BODY, fontSize=8.4, leading=11.2, spaceAfter=0))
        _, bh = body_p.wrap(cw-26, ch-th-25); body_p.drawOn(c, x+13, y+ch-28-th-bh)

    card(30, 625, 260, 100, "WHAT DOES IT DO?",
         "Uses live ADS-B data to show aircraft near your chosen location, plotted by direction and distance.", "#EFF6FA", "#EFF6FA", "#059447")
    card(305, 625, 260, 100, "IMPORTANT - TEST DEVICE",
         "This is a test / experimental device. It can get warm during normal operation. <b>Do not leave it plugged in and unattended; unplug it when not in use.</b> Use responsibly.", "#FFF5F3", "#EF3038", "#EF3038")
    card(30, 480, 260, 120, "1  POWER",
         "<font color='#059447'><b>Bring your own USB-C data cable.</b></font><br/><br/>Use a suitable USB power source. The device may feel warm in use; this is normal.")
    card(305, 480, 260, 120, "2  CONNECT TO RADAR WI-FI",
         "On your phone or computer join:<br/><br/><b>PlaneRadar-Setup</b><br/><br/>Then open <b>http://192.168.4.1</b> in a browser.")
    card(30, 320, 260, 135, "2.4 GHz WI-FI ONLY",
         "The radar cannot connect to a 5 GHz-only network. Choose a 2.4 GHz network and enter its name, password and radar location.", "#FFF5D9", "#FFF5D9")
    card(305, 320, 260, 135, "GET YOUR LATITUDE & LONGITUDE",
         "Search for a location or select a point on a map, then copy its coordinates. The detailed pages explain airport selection too.", "#EFF6FA", "#EFF6FA")
    code = qr.QrCodeWidget("https://www.latlong.net/")
    bounds = code.getBounds(); size = 66
    drawing = Drawing(size, size, transform=[size/(bounds[2]-bounds[0]),0,0,size/(bounds[3]-bounds[1]),0,0])
    drawing.add(code); drawing.drawOn(c, 485, 333)
    card(30, 155, 260, 140, "3  ENTER YOUR LOCATION",
         "Use decimal degrees with up to six decimal places.<br/><br/><font color='#059447'>Public example - Heathrow Airport:<br/>51.470020&nbsp;&nbsp; -0.454295</font><br/><br/>North/East are positive; South/West are negative.", "#EFF8F1", "#EFF8F1")
    card(305, 155, 260, 140, "4  BUTTON CONTROLS",
         "<font color='#059447'><b>SHORT PRESS</b></font> - cycle 5, 10, 15 and 25 km.<br/><br/><font color='#EF3038'><b>HOLD FOR 3 SECONDS</b></font> - wipes saved Wi-Fi, location and units, then restarts setup.")
    c.setFillColor(colors.HexColor("#081D39")); c.rect(0, 0, w, 42, fill=1, stroke=0)
    c.setFillColor(colors.HexColor("#52E879")); c.setFont("Helvetica-Bold", 10); c.drawString(30, 17, "READY TO TRACK")
    c.setFillColor(colors.white); c.setFont("Helvetica", 7.5); c.drawString(132, 17, f"After setup, the radar reconnects automatically. Firmware {version}  |  1 / 5")
    c.save()


def build(version, output):
    output.parent.mkdir(parents=True, exist_ok=True)
    def page(canvas, doc):
        canvas.saveState(); canvas.setStrokeColor(TEAL); canvas.setLineWidth(2)
        canvas.line(40, A4[1] - 35, A4[0] - 40, A4[1] - 35)
        canvas.setFont("Helvetica-Bold", 8); canvas.setFillColor(NAVY)
        canvas.drawString(40, A4[1] - 27, "PLANE RADAR  /  USER MANUAL")
        canvas.setFont("Helvetica", 8); canvas.setFillColor(MUTED)
        canvas.drawString(40, 27, f"Firmware {version}  |  ESP32-C3 / GC9A01")
        canvas.drawRightString(A4[0] - 40, 27, f"{doc.page + 1} / 5"); canvas.restoreState()

    story = [p("Plane Radar user manual", TITLE),
      p("Daily use, web portal and settings", HEAD),
      p(f"For custom firmware <b>{version}</b> on the ESP32-C3 Super Mini with a 240 x 240 round GC9A01 display."),
      box("<b>Quick start:</b> power by USB-C, wait for Wi-Fi, then open <link href='http://plane-radar.local' color='#087F79'><b>http://plane-radar.local</b></link> from a phone or computer on the same network."),
      p("What the screen shows", HEAD),
      p("The centre is your saved radar location. Concentric rings show distance; the outer edge uses the active range. Aircraft positions come from the adsb.fi internet feed. A nose symbol shows heading, and the short vector indicates recent direction of travel. Aircraft outside the circle can appear as edge dots."),
      p("Green runway lines reproduce the approximate runway headings and pattern of nearby airports. Optional ICAO codes identify them. Emergency aircraft are shown orange-red when the feed reports an emergency state or squawk 7500, 7600 or 7700."),
      p("The one button", HEAD),
      rows([("Short press", "Cycles the radar range through 5, 10, 15 and 25 km. The selected range is saved."),
            ("Five rapid presses", "Shows this radar's permanent four-character device ID and current IP address for five seconds. Useful when several radars share one network."),
            ("Hold for 3 seconds", "Clears Wi-Fi credentials, radar location and distance units, then restarts setup. Use deliberately: this is not a normal menu button."),
            ("Hold while plugging in", "Enters ESP32 USB download mode for the one-time original-firmware migration. The screen may remain blank.")]),
      box("<b>Important:</b> do not hold the button during normal use unless you intend to reset setup information.", WARM),
      p("First Wi-Fi setup", HEAD),
      p("If no network is saved, the radar creates <b>PlaneRadar-Setup</b>. Join it from your phone or computer. The captive page may open automatically; otherwise browse to <b>http://192.168.4.1</b>. Choose a 2.4 GHz Wi-Fi network, enter its password and save. The ESP32-C3 cannot join a 5 GHz-only network."),
      PageBreak(),
      p("Open the local web portal", TITLE),
      p("Normal connection", HEAD),
      p("Connect your phone, tablet or computer to the same local Wi-Fi as the radar. Open <b>http://plane-radar.local</b> in a browser. This connection travels over your local network; USB does not need to be connected to the computer."),
      p("If plane-radar.local does not open", HEAD),
      p("Find the radar in your router's connected-device list and open its numeric address, for example <b>http://192.168.0.150</b>. Make sure mobile data, a VPN, guest-network isolation or a corporate firewall is not preventing access to local devices. `.local` discovery can take a little longer immediately after reboot."),
      p("Connecting multiple radars to the same network", HEAD),
      p("Connect each radar normally to the same 2.4 GHz Wi-Fi network. All units deliberately use the friendly address <b>plane-radar.local</b>, so that name cannot reliably choose a particular radar when several are online. Open each unit using its individual numeric IP address and bookmark it."),
      p("To identify a physical unit, press its button <b>five times rapidly</b>, leaving no long pause between presses. The radar temporarily clears the normal display and shows a name such as <b>Plane Radar 25E0</b>, its four-character <b>Device ID</b>, and its current <b>IP address</b> for five seconds. The normal radar then returns automatically; Wi-Fi and aircraft collection continue in the background."),
      box("The Device ID is derived from the final four characters of that ESP32's permanent Wi-Fi MAC address. It survives firmware updates and settings resets. Four presses cycle through all four ranges before the fifth opens the identity screen, so the final selected range is unchanged."),
      p("Status panel", HEAD),
      rows([("Firmware version", "The software currently running. Check this after an update."),
            ("Wi-Fi", "Whether the radar is connected to its saved network."),
            ("IP address", "The direct local-network address; useful as a bookmark and for multiple radars."),
            ("Device address", "The friendly mDNS name, normally plane-radar.local."),
            ("Latitude / longitude", "The centre used for the radar display and aircraft query.")]),
      p("Saving changes", HEAD),
      p("Each card has its own Save button. A green confirmation means the radar accepted and stored that card. Settings survive a normal reboot and OTA firmware update. Save one card before moving to another."),
      box("The portal is plain HTTP on your trusted local network. It is not exposed to the internet by the radar. Browser geolocation requires HTTPS, so location is entered by coordinates or airport instead."),
      PageBreak(),
      p("Radar settings", TITLE),
      p("Orientation and appearance", HEAD),
      rows([("Top of radar", "Choose N, E, S or W to put North, East, South or West at the top. This rotates the map presentation, not aircraft data."),
            ("Dim radar rings", "Reduces ring brightness by 50%. Aircraft, labels, runway lines and crosshairs remain prominent."),
            ("Decorative sweep", "Adds a thin green sweep completing one rotation every five seconds. It is decorative; aircraft updates remain independent.")]),
      p("Plane labels - choose up to three", HEAD),
      rows([("Callsign", "Flight identifier; falls back to the aircraft hex code when absent."),
            ("Aircraft type", "Type designator such as A320 when supplied."),
            ("Altitude", "Barometric altitude, geometric fallback, or GND."),
            ("Registration", "Civil registration such as G-ABCD when available."),
            ("Ground speed", "Speed over the ground in knots."),
            ("Climb/descent", "Signed vertical rate in feet per minute."),
            ("Squawk", "Four-digit transponder code prefixed SQ."),
            ("Category", "Compact ADS-B aircraft category code."),
            ("Navigation modes", "Compact AP, ALT, LNAV, VNAV, APP and TCAS flags; + means more could not fit."),
            ("Military marker", "Shows MIL only when the source flags the aircraft as military.")]),
      p("Unavailable fields are omitted rather than replaced with guesses. Selecting a fourth option is blocked until one is deselected. Emergency colouring is automatic and does not consume a label slot."),
      p("Airport overlay", HEAD),
      p("<b>Runway layout</b> controls thin green runway lines; <b>ICAO labels</b> controls airport codes. Coverage includes worldwide major airports and UK fixed-wing airfields. Only runways within the displayed area are drawn."),
      PageBreak(),
      p("Location, updates and help", TITLE),
      p("Set the radar location", HEAD),
      p("For an airport, enter its four-character ICAO code in <b>Set base from ICAO airport</b>, select a suggestion and press <b>Use airport</b>. This is useful for watching traffic around an airport rather than your physical location."),
      p("For any other point, use <b>Find coordinates on map</b>, search or place a point, copy latitude and longitude, then press <b>Save location</b>. Latitude must be -90 to 90 and longitude -180 to 180. Aircraft and runway positions refresh around the new centre."),
      p("Wi-Fi and radar settings links", HEAD),
      p("The bottom links open WiFiManager pages. <b>Wi-Fi setup</b> changes network credentials. <b>Radar settings</b> exposes the legacy location, miles/km and runway controls. Prefer the main portal cards for the newer display options."),
      p("Firmware update over Wi-Fi", HEAD),
      p("Download the newer <b>*-ota.bin</b> or application <b>firmware.bin</b> from the custom project's trusted release. In Firmware, choose it and press <b>Install update</b>. Keep power and Wi-Fi stable while progress advances. A successful upload writes the inactive application slot, reboots, and then reports the new version. The one-time Mac USB installer uses an animated ASCII indicator during long stages and an explicit <b>y/n</b> confirmation before writing."),
      box("Upload only the OTA/application file. Never upload a factory, merged, bootloader, partitions or private backup file through the web portal.", WARM),
      p("Troubleshooting", HEAD),
      rows([("No aircraft", "Confirm Wi-Fi shows Connected, the radar location is correct, and the internet connection works. The public feed may occasionally be unavailable."),
            ("Portal unavailable", "Try the IP address, disable VPN/mobile-data switching, confirm both devices use the same non-guest network, then reboot the radar."),
            ("Runways absent", "Enable Runway layout, choose a range that reaches the airport, and verify the selected location."),
            ("Labels absent", "Save one to three choices. A selected field is omitted for aircraft that do not report it."),
            ("Update failed", "The working firmware should remain active after an incomplete OTA upload. Refresh the portal and retry with the correct application image.")]),
      p("Support and sources", HEAD),
      p("Custom firmware: <link href='https://github.com/smgam29/ESP32-Plane-Radar' color='#087F79'>github.com/smgam29/ESP32-Plane-Radar</link><br/>Original project: <link href='https://github.com/MatixYo/ESP32-Plane-Radar' color='#087F79'>github.com/MatixYo/ESP32-Plane-Radar</link>", SMALL)]

    with tempfile.TemporaryDirectory(prefix="plane-radar-manual-") as temporary:
        flyer = Path(temporary) / "flyer.pdf"
        detail = Path(temporary) / "detail.pdf"
        draw_quick_flyer(flyer, version)
        doc = SimpleDocTemplate(str(detail), pagesize=A4, rightMargin=40, leftMargin=40,
            topMargin=51, bottomMargin=45, title="Plane Radar User Manual",
            author="Plane Radar custom firmware project")
        doc.build(story, onFirstPage=page, onLaterPages=page)
        if doc.page != 4: raise ValueError(f"Expected four detail pages, got {doc.page}")
        writer = PdfWriter()
        writer.append(str(flyer)); writer.append(str(detail))
        writer.add_metadata({"/Title": "Plane Radar User Manual",
                             "/Author": "Plane Radar custom firmware project"})
        with output.open("wb") as stream: writer.write(stream)
    print(output)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--version", required=True)
    parser.add_argument("--output", type=Path, default=Path("output/pdf/Plane-Radar-User-Manual-A4.pdf"))
    args = parser.parse_args(); build(args.version, args.output)
