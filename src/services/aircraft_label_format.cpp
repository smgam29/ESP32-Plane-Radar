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
void formatLabel(JsonObjectConst plane, Label label, char* text, size_t size) {
  text[0] = '\0';
  double value = 0;
  switch (label) {
    case Label::Callsign:
      copyText(plane["flight"], text, size);
      if (!text[0]) copyText(plane["hex"], text, size);
      break;
    case Label::AircraftType: copyText(plane["t"], text, size); break;
    case Label::Registration: copyText(plane["r"], text, size); break;
    case Label::Altitude:
      if (strcmp(plane["alt_baro"] | "", "ground") == 0) {
        snprintf(text, size, "GND");
      } else if (number(plane["alt_baro"], value) || number(plane["alt_geom"], value)) {
        if (value >= -2000 && value <= 200000) snprintf(text, size, "%.0f ft", value);
      }
      break;
    case Label::GroundSpeed:
      // Airspeed is not a substitute for ground speed.
      if (number(plane["gs"], value) && value >= 0 && value <= 10000)
        snprintf(text, size, "%.0f kt", value);
      break;
    case Label::VerticalRate:
      if (number(plane["baro_rate"], value) || number(plane["geom_rate"], value))
        if (std::abs(value) <= 100000) snprintf(text, size, "%+.0f ft/m", value);
      break;
    case Label::Squawk: {
      char code[8] = {};
      copyText(plane["squawk"], code, sizeof(code));
      if (code[0]) snprintf(text, size, "SQ %s", code);
      break;
    }
    case Label::Category: {
      char code[4] = {};
      copyText(plane["category"], code, sizeof(code));
      if (code[0]) snprintf(text, size, "CAT %s", code);
      break;
    }
    case Label::Navigation: {
      const char* names[] = {"autopilot", "althold", "lnav", "vnav", "approach", "tcas"};
      const char* flags[] = {"AP", "ALT", "LNAV", "VNAV", "APP", "TCAS"};
      const JsonArrayConst modes = plane["nav_modes"].as<JsonArrayConst>();
      for (size_t i = 0; i < 6; ++i) {
        bool found = false;
        for (JsonVariantConst mode : modes)
          if (strcmp(mode | "", names[i]) == 0) found = true;
        if (!found) continue;
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
      if ((plane["dbFlags"].as<unsigned>() & 1U) != 0) snprintf(text, size, "MIL");
      break;
    default: break;
  }
}
}  // namespace

void formatAircraftLabels(JsonObjectConst plane, uint16_t mask, AircraftLabels& out) {
  out = {};
  out.emergency = isEmergency(plane);
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
}  // namespace services::adsb
