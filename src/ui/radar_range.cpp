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
constexpr char kPrefsDimRingsKey[] = "dimRings";
constexpr char kPrefsSweepKey[] = "sweep";
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

uint8_t s_range_index = kDefaultRangeIndex;
bool s_use_miles = false;
bool s_dim_rings = false;
bool s_sweep_enabled = false;
bool s_show_runways = true;
bool s_show_runway_labels = true;
TopDirection s_top_direction = TopDirection::North;
uint16_t s_label_mask = kAllLabels;

bool openPrefs(Preferences& prefs, bool read_only) {
  if (prefs.begin(kPrefsNamespace, read_only)) {
    return true;
  }
  Serial.printf("planeradar: NVS open failed (%s)\n",
                read_only ? "read" : "write");
  return false;
}

bool saveRangeIndex() {
  Preferences prefs;
  if (!openPrefs(prefs, false)) {
    return false;
  }
  const bool saved =
      prefs.putUChar(kPrefsRangeKey, s_range_index) == sizeof(s_range_index);
  prefs.end();
  return saved;
}

bool saveUseMiles() {
  Preferences prefs;
  if (!openPrefs(prefs, false)) {
    return false;
  }
  const bool saved = prefs.putBool(kPrefsMilesKey, s_use_miles) == 1;
  prefs.end();
  return saved;
}

bool saveShowRunways() {
  Preferences prefs;
  if (!openPrefs(prefs, false)) {
    return false;
  }
  const bool saved = prefs.putBool(kPrefsRunwaysKey, s_show_runways) == 1;
  prefs.end();
  return saved;
}

bool saveShowRunwayLabels() {
  Preferences prefs;
  if (!openPrefs(prefs, false)) {
    return false;
  }
  const bool saved =
      prefs.putBool(kPrefsRunwayLabelsKey, s_show_runway_labels) == 1;
  prefs.end();
  return saved;
}

bool persistTopDirection() {
  Preferences prefs;
  if (!openPrefs(prefs, false)) {
    return false;
  }
  const uint8_t value = static_cast<uint8_t>(s_top_direction);
  const bool saved = prefs.putUChar(kPrefsTopDirectionKey, value) == sizeof(value);
  prefs.end();
  return saved;
}

bool persistLabelMask(uint16_t mask) {
  Preferences prefs;
  if (!openPrefs(prefs, false)) {
    return false;
  }
  const bool saved =
      prefs.putUShort(kPrefsLabelMaskV2Key, mask) == sizeof(mask);
  prefs.end();
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
  Preferences prefs;
  if (!openPrefs(prefs, true)) {
    Serial.println("planeradar: using defaults until settings are saved");
    return;
  }
  const uint8_t saved = prefs.getUChar(kPrefsRangeKey, kDefaultRangeIndex);
  s_range_index =
      (saved < kRangePresetCount) ? saved : kDefaultRangeIndex;
  s_use_miles = prefs.getBool(kPrefsMilesKey, false);
  s_dim_rings = prefs.getBool(kPrefsDimRingsKey, false);
  s_sweep_enabled = prefs.getBool(kPrefsSweepKey, false);
  s_show_runways = prefs.getBool(kPrefsRunwaysKey, true);
  s_show_runway_labels = prefs.getBool(kPrefsRunwayLabelsKey, true);
  const uint8_t saved_direction =
      prefs.getUChar(kPrefsTopDirectionKey,
                     static_cast<uint8_t>(TopDirection::North));
  s_top_direction = saved_direction <= static_cast<uint8_t>(TopDirection::West)
                        ? static_cast<TopDirection>(saved_direction)
                        : TopDirection::North;
  const uint16_t legacy = prefs.getUChar(kPrefsLabelMaskKey, kAllLabels) & kAllLabels;
  const uint16_t saved_labels = prefs.getUShort(kPrefsLabelMaskV2Key, legacy);
  s_label_mask = services::adsb::validLabelMask(saved_labels) ? saved_labels : legacy;
  prefs.end();
  Serial.printf(
      "planeradar: restored range=%u miles=%d dim=%d sweep=%d top=%s labels=%u\n",
      s_range_index, s_use_miles ? 1 : 0, s_dim_rings ? 1 : 0,
      s_sweep_enabled ? 1 : 0, topDirectionCode(),
      static_cast<unsigned>(s_label_mask));
}

void rangeNext() {
  s_range_index = static_cast<uint8_t>((s_range_index + 1) % kRangePresetCount);
  if (!saveRangeIndex()) {
    Serial.println("planeradar: range index not persisted");
  }
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

bool sweepEnabled() { return s_sweep_enabled; }

bool saveSweepEnabled(bool enabled) {
  Preferences prefs;
  if (!openPrefs(prefs, false)) {
    return false;
  }
  const bool saved = prefs.putBool(kPrefsSweepKey, enabled) == 1;
  prefs.end();
  if (saved) {
    s_sweep_enabled = enabled;
  }
  return saved;
}

bool dimRings() { return s_dim_rings; }

bool saveDimRings(bool enabled) {
  Preferences prefs;
  if (!openPrefs(prefs, false)) {
    return false;
  }
  const bool saved = prefs.putBool(kPrefsDimRingsKey, enabled) == 1;
  prefs.end();
  if (saved) {
    s_dim_rings = enabled;
  }
  return saved;
}

bool showRunways() { return s_show_runways; }

bool showRunwayLabels() { return s_show_runway_labels; }

bool saveAirportOverlay(bool runways, bool labels) {
  const bool previous_runways = s_show_runways;
  const bool previous_labels = s_show_runway_labels;
  s_show_runways = runways;
  s_show_runway_labels = labels;
  if (!saveShowRunways() || !saveShowRunwayLabels()) {
    s_show_runways = previous_runways;
    s_show_runway_labels = previous_labels;
    Serial.println("planeradar: airport overlay not persisted");
    return false;
  }
  Serial.printf("Airport overlay: runways=%s labels=%s\n",
                runways ? "on" : "off", labels ? "on" : "off");
  return true;
}

bool showCallsign() { return (s_label_mask & kLabelCallsign) != 0; }

bool showAircraftType() { return (s_label_mask & kLabelAircraftType) != 0; }

bool showAltitude() { return (s_label_mask & kLabelAltitude) != 0; }

uint16_t labelMask() { return s_label_mask; }

bool saveLabelMask(uint16_t mask) {
  if (!services::adsb::validLabelMask(mask) || !persistLabelMask(mask)) {
    return false;
  }
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
  TopDirection next = TopDirection::North;
  switch (direction[0]) {
    case 'N':
    case 'n':
      next = TopDirection::North;
      break;
    case 'E':
    case 'e':
      next = TopDirection::East;
      break;
    case 'S':
    case 's':
      next = TopDirection::South;
      break;
    case 'W':
    case 'w':
      next = TopDirection::West;
      break;
    default:
      return false;
  }
  const TopDirection previous = s_top_direction;
  s_top_direction = next;
  if (!persistTopDirection()) {
    s_top_direction = previous;
    Serial.println("planeradar: orientation not persisted");
    return false;
  }
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
  const bool previous = s_use_miles;
  s_use_miles = portalCheckboxChecked(checkbox_value);
  if (!saveUseMiles()) {
    s_use_miles = previous;
    Serial.println("planeradar: miles preference not persisted");
    return;
  }
  Serial.printf("Distance units: %s\n", s_use_miles ? "miles" : "km");
}

void saveRunwaysFromPortal(const char* checkbox_value) {
  const bool previous = s_show_runways;
  s_show_runways = portalCheckboxChecked(checkbox_value);
  if (!saveShowRunways()) {
    s_show_runways = previous;
    Serial.println("planeradar: runway preference not persisted");
    return;
  }
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
  s_sweep_enabled = false;
  s_use_miles = false;
  s_dim_rings = false;
  s_show_runways = true;
  s_show_runway_labels = true;
  Preferences prefs;
  if (openPrefs(prefs, false)) {
    prefs.remove(kPrefsMilesKey);
    prefs.remove(kPrefsDimRingsKey);
    prefs.remove(kPrefsSweepKey);
    prefs.remove(kPrefsRunwaysKey);
    prefs.remove(kPrefsRunwayLabelsKey);
    prefs.remove(kPrefsTopDirectionKey);
    prefs.remove(kPrefsLabelMaskKey);
    prefs.remove(kPrefsLabelMaskV2Key);
    prefs.end();
  }
  s_top_direction = TopDirection::North;
  s_label_mask = kAllLabels;
}

}  // namespace ui::radar
