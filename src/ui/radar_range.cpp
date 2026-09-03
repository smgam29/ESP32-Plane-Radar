#include "ui/radar_range.h"

#include "ui/radar_theme.h"
#include "services/aircraft_labels.h"

#include <Preferences.h>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace ui::radar {

namespace {

constexpr char kPrefsNamespace[] = "planeradar";
constexpr char kPrefsRangeKey[] = "rangeIdx";
constexpr char kPrefsMilesKey[] = "useMiles";
constexpr char kPrefsRunwaysKey[] = "showRwys";
constexpr char kPrefsRunwayLabelsKey[] = "showRwLbl";
constexpr char kPrefsTopDirectionKey[] = "topDir";
constexpr char kPrefsLabelMaskKey[] = "labelMask";  // Legacy uint8 value.
constexpr char kPrefsLabelMaskV2Key[] = "labelsV2";
constexpr uint8_t kDefaultRangeIndex = 1;  // 10 km ring
constexpr uint8_t kLabelCallsign = 1U << 0;
constexpr uint8_t kLabelAircraftType = 1U << 1;
constexpr uint8_t kLabelAltitude = 1U << 2;
constexpr uint8_t kAllLabels =
    kLabelCallsign | kLabelAircraftType | kLabelAltitude;
constexpr float kKmPerMile = 1.609344f;

Preferences s_prefs;
uint8_t s_range_index = kDefaultRangeIndex;
bool s_use_miles = false;
bool s_show_runways = true;
bool s_show_runway_labels = true;
TopDirection s_top_direction = TopDirection::North;
uint16_t s_label_mask = kAllLabels;

void saveRangeIndex() {
  if (!s_prefs.begin(kPrefsNamespace, false)) {
    return;
  }
  s_prefs.putUChar(kPrefsRangeKey, s_range_index);
  s_prefs.end();
}

void saveUseMiles() {
  if (!s_prefs.begin(kPrefsNamespace, false)) {
    return;
  }
  s_prefs.putBool(kPrefsMilesKey, s_use_miles);
  s_prefs.end();
}

void saveShowRunways() {
  if (!s_prefs.begin(kPrefsNamespace, false)) {
    return;
  }
  s_prefs.putBool(kPrefsRunwaysKey, s_show_runways);
  s_prefs.end();
}

void saveShowRunwayLabels() {
  if (!s_prefs.begin(kPrefsNamespace, false)) {
    return;
  }
  s_prefs.putBool(kPrefsRunwayLabelsKey, s_show_runway_labels);
  s_prefs.end();
}

void persistTopDirection() {
  if (!s_prefs.begin(kPrefsNamespace, false)) {
    return;
  }
  s_prefs.putUChar(kPrefsTopDirectionKey,
                   static_cast<uint8_t>(s_top_direction));
  s_prefs.end();
}

bool persistLabelMask(uint16_t mask) {
  if (!s_prefs.begin(kPrefsNamespace, false)) return false;
  const bool saved = s_prefs.putUShort(kPrefsLabelMaskV2Key, mask) == sizeof(mask);
  s_prefs.end();
  return saved;
}

bool portalCheckboxChecked(const char* value) {
  if (value == nullptr || value[0] == '\0') {
    return false;
  }
  // WiFiManager checkbox submits its value= attribute ("T", or "F" if we prefilled F).
  if ((value[0] == 'T' || value[0] == 't' || value[0] == 'F' || value[0] == 'f') &&
      value[1] == '\0') {
    return true;
  }
  return strcmp(value, "on") == 0;
}

}  // namespace

void rangeInit() {
  if (!s_prefs.begin(kPrefsNamespace, true)) {
    return;
  }
  const uint8_t saved = s_prefs.getUChar(kPrefsRangeKey, kDefaultRangeIndex);
  s_range_index =
      (saved < kRangePresetCount) ? saved : kDefaultRangeIndex;
  s_use_miles = s_prefs.getBool(kPrefsMilesKey, false);
  s_show_runways = s_prefs.getBool(kPrefsRunwaysKey, true);
  s_show_runway_labels = s_prefs.getBool(kPrefsRunwayLabelsKey, true);
  const uint8_t saved_direction =
      s_prefs.getUChar(kPrefsTopDirectionKey,
                       static_cast<uint8_t>(TopDirection::North));
  s_top_direction = saved_direction <= static_cast<uint8_t>(TopDirection::West)
                        ? static_cast<TopDirection>(saved_direction)
                        : TopDirection::North;
  const uint16_t legacy = s_prefs.getUChar(kPrefsLabelMaskKey, kAllLabels) & kAllLabels;
  const uint16_t saved_labels = s_prefs.getUShort(kPrefsLabelMaskV2Key, legacy);
  s_label_mask = services::adsb::validLabelMask(saved_labels) ? saved_labels : legacy;
  s_prefs.end();
}

void rangeNext() {
  s_range_index = static_cast<uint8_t>((s_range_index + 1) % kRangePresetCount);
  saveRangeIndex();
}

const RangePreset& rangeCurrent() { return kRangePresets[s_range_index]; }

uint8_t rangeIndex() { return s_range_index; }

float fetchRadiusKm() {
  const float outer_km = rangeCurrent().outer_km;
  const float screen_r_px =
      static_cast<float>(kCenterX - kBeyondRingScreenMarginPx);
  return outer_km * (screen_r_px / static_cast<float>(kGridOuterRadius));
}

bool useMiles() { return s_use_miles; }

bool showRunways() { return s_show_runways; }

bool showRunwayLabels() { return s_show_runway_labels; }

void saveAirportOverlay(bool runways, bool labels) {
  s_show_runways = runways;
  s_show_runway_labels = labels;
  saveShowRunways();
  saveShowRunwayLabels();
  Serial.printf("Airport overlay: runways=%s labels=%s\n",
                runways ? "on" : "off", labels ? "on" : "off");
}

bool showCallsign() { return (s_label_mask & kLabelCallsign) != 0; }

bool showAircraftType() { return (s_label_mask & kLabelAircraftType) != 0; }

bool showAltitude() { return (s_label_mask & kLabelAltitude) != 0; }

uint16_t labelMask() { return s_label_mask; }

bool saveLabelMask(uint16_t mask) {
  if (!services::adsb::validLabelMask(mask) || !persistLabelMask(mask)) return false;
  s_label_mask = mask;
  return true;
}

TopDirection topDirection() { return s_top_direction; }

const char* topDirectionCode() {
  switch (s_top_direction) {
    case TopDirection::East:
      return "E";
    case TopDirection::South:
      return "S";
    case TopDirection::West:
      return "W";
    default:
      return "N";
  }
}

bool saveTopDirection(const char* direction) {
  if (direction == nullptr || direction[0] == '\0' || direction[1] != '\0') {
    return false;
  }
  switch (direction[0]) {
    case 'N':
    case 'n':
      s_top_direction = TopDirection::North;
      break;
    case 'E':
    case 'e':
      s_top_direction = TopDirection::East;
      break;
    case 'S':
    case 's':
      s_top_direction = TopDirection::South;
      break;
    case 'W':
    case 'w':
      s_top_direction = TopDirection::West;
      break;
    default:
      return false;
  }
  persistTopDirection();
  Serial.printf("Radar top direction: %s\n", topDirectionCode());
  return true;
}

void orientOffset(float east, float north, float* screen_right,
                  float* screen_up) {
  switch (s_top_direction) {
    case TopDirection::East:
      *screen_right = -north;
      *screen_up = east;
      break;
    case TopDirection::South:
      *screen_right = -east;
      *screen_up = -north;
      break;
    case TopDirection::West:
      *screen_right = north;
      *screen_up = -east;
      break;
    default:
      *screen_right = east;
      *screen_up = north;
      break;
  }
}

float orientHeading(float heading_deg) {
  const float oriented =
      heading_deg - static_cast<float>(static_cast<uint8_t>(s_top_direction)) *
                        90.0f;
  return oriented < 0.0f ? oriented + 360.0f : oriented;
}

void saveMilesFromPortal(const char* checkbox_value) {
  s_use_miles = portalCheckboxChecked(checkbox_value);
  saveUseMiles();
  Serial.printf("Distance units: %s\n", s_use_miles ? "miles" : "km");
}

void saveRunwaysFromPortal(const char* checkbox_value) {
  s_show_runways = portalCheckboxChecked(checkbox_value);
  saveShowRunways();
  Serial.printf("Runway overlay: %s\n", s_show_runways ? "on" : "off");
}

void formatRing3Label(char* buf, size_t len, float ring3_km, bool use_miles) {
  if (use_miles) {
    const int mi = static_cast<int>(lroundf(ring3_km / kKmPerMile));
    snprintf(buf, len, "%dmi", mi);
  } else {
    const int km = static_cast<int>(lroundf(ring3_km));
    snprintf(buf, len, "%dkm", km);
  }
}

void formatCurrentRing3Label(char* buf, size_t len) {
  formatRing3Label(buf, len, rangeCurrent().ring3_km, s_use_miles);
}

void unitsReset() {
  s_use_miles = false;
  s_show_runways = true;
  s_show_runway_labels = true;
  if (s_prefs.begin(kPrefsNamespace, false)) {
    s_prefs.remove(kPrefsMilesKey);
    s_prefs.remove(kPrefsRunwaysKey);
    s_prefs.remove(kPrefsRunwayLabelsKey);
    s_prefs.remove(kPrefsTopDirectionKey);
    s_prefs.remove(kPrefsLabelMaskKey);
    s_prefs.remove(kPrefsLabelMaskV2Key);
    s_prefs.end();
  }
  s_top_direction = TopDirection::North;
  s_label_mask = kAllLabels;
}

}  // namespace ui::radar
