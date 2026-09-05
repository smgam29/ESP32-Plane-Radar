#include "services/aircraft_label_format.h"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace services::adsb {
namespace {
void copyText(JsonVariantConst value, char* out, size_t size) {
  const char* text = value.as<const char*>();
  if (!text) return;
  size_t n = 0;
  // Keep API text printable and bounded for the tiny display.
  while (*text && n + 1 < size) {
    const unsigned char c = *text++;
    if (c >= 32 && c <= 126) out[n++] = c;
  }
  while (n && out[n - 1] == ' ') --n;
  out[n] = '\0';
}
bool number(JsonVariantConst value, double& out) {
  if (!value.is<double>()) return false;
  out = value.as<double>();
  return std::isfinite(out);
}
bool isEmergency(JsonObjectConst plane) {
  const char* status = plane["emergency"] | "";
  const char* active[] = {"general", "lifeguard", "minfuel", "nordo", "unlawful", "downed"};
  for (const char* name : active) if (strcmp(status, name) == 0) return true;
  const char* squawk = plane["squawk"] | "";
  return strcmp(squawk, "7500") == 0 || strcmp(squawk, "7600") == 0 ||
         strcmp(squawk, "7700") == 0;
}
void formatLabel(const AircraftLabelData& plane, Label label, char* text, size_t size) {
  text[0] = '\0';
  switch (label) {
    case Label::Callsign:
      snprintf(text, size, "%s", plane.callsign);
      break;
    case Label::AircraftType: snprintf(text, size, "%s", plane.aircraftType); break;
    case Label::Registration: snprintf(text, size, "%s", plane.registration); break;
    case Label::Altitude:
      if (plane.ground) {
        snprintf(text, size, "GND");
      } else if (std::isfinite(plane.altitude)) {
        snprintf(text, size, "%.0f ft", plane.altitude);
      }
      break;
    case Label::GroundSpeed:
      // Airspeed is not a substitute for ground speed.
      if (std::isfinite(plane.groundSpeed))
        snprintf(text, size, "%.0f kt", plane.groundSpeed);
      break;
    case Label::VerticalRate:
      if (std::isfinite(plane.verticalRate))
        snprintf(text, size, "%+.0f ft/m", plane.verticalRate);
      break;
    case Label::Squawk: {
      const char* code = plane.squawk;
      if (code[0]) snprintf(text, size, "SQ %s", code);
      break;
    }
    case Label::Category: {
      const char* code = plane.category;
      if (code[0]) snprintf(text, size, "CAT %s", code);
      break;
    }
    case Label::Navigation: {
      const char* flags[] = {"AP", "ALT", "LNAV", "VNAV", "APP", "TCAS"};
      for (size_t i = 0; i < 6; ++i) {
        if (!(plane.navigation & (1U << i))) continue;
        const size_t used = strlen(text);
        // Reserve room for a '+' when more modes cannot fit.
        if (used + strlen(flags[i]) + (used ? 1 : 0) + 2 > size) {
          snprintf(text + used, size - used, "+");
          break;
        }
        snprintf(text + used, size - used, "%s%s", used ? " " : "", flags[i]);
      }
      break;
    }
    case Label::Military:
      if (plane.military) snprintf(text, size, "MIL");
      break;
    default: break;
  }
}
}  // namespace

void formatAircraftLabels(const AircraftLabelData& plane, uint16_t mask, AircraftLabels& out) {
  out = {};
  out.emergency = plane.emergency;
  if (!validLabelMask(mask)) return;
  for (uint8_t i = 0; i < kLabelCount && out.count < kMaxLabels; ++i) {
    const Label label = static_cast<Label>(i);
    if (!(mask & labelBit(label))) continue;
    auto& line = out.lines[out.count];
    line.kind = label;
    formatLabel(plane, label, line.text, sizeof(line.text));
    if (line.text[0]) ++out.count;
  }
}

void readAircraftLabelData(JsonObjectConst plane, AircraftLabelData& out) {
  out = {};
  out.altitude = out.groundSpeed = out.verticalRate = NAN;
  copyText(plane["flight"], out.callsign, sizeof(out.callsign));
  if (!out.callsign[0]) copyText(plane["hex"], out.callsign, sizeof(out.callsign));
  copyText(plane["t"], out.aircraftType, sizeof(out.aircraftType));
  copyText(plane["r"], out.registration, sizeof(out.registration));
  copyText(plane["squawk"], out.squawk, sizeof(out.squawk));
  copyText(plane["category"], out.category, sizeof(out.category));
  out.ground = strcmp(plane["alt_baro"] | "", "ground") == 0;
  out.emergency = isEmergency(plane);
  out.military = (plane["dbFlags"].as<unsigned>() & 1U) != 0;
  double value = 0;
  if ((number(plane["alt_baro"], value) || number(plane["alt_geom"], value)) &&
      value >= -2000 && value <= 200000) out.altitude = value;
  if (number(plane["gs"], value) && value >= 0 && value <= 10000)
    out.groundSpeed = value;
  if ((number(plane["baro_rate"], value) || number(plane["geom_rate"], value)) &&
      std::abs(value) <= 100000) out.verticalRate = value;
  const char* names[] = {"autopilot", "althold", "lnav", "vnav", "approach", "tcas"};
  for (JsonVariantConst mode : plane["nav_modes"].as<JsonArrayConst>())
    for (uint8_t i = 0; i < 6; ++i)
      if (strcmp(mode | "", names[i]) == 0) out.navigation |= 1U << i;
}

void formatAircraftLabels(JsonObjectConst plane, uint16_t mask, AircraftLabels& out) {
  AircraftLabelData data;
  readAircraftLabelData(plane, data);
  formatAircraftLabels(data, mask, out);
}
}  // namespace services::adsb
