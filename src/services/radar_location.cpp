#include "services/radar_location.h"

#include <Preferences.h>
#include <cmath>
#include <cstdlib>
#include <cstring>

#include "config.h"

namespace services::location {

namespace {

constexpr char kPrefsNamespace[] = "radar";
/** Legacy blob keys from Preferences::putDouble (migrated on load). */
constexpr char kKeyLat[] = "lat";
constexpr char kKeyLon[] = "lon";
/** Preferred integer microdegree storage — survives power loss reliably. */
constexpr char kKeyLatE7[] = "latE7";
constexpr char kKeyLonE7[] = "lonE7";

double s_lat = config::kDefaultRadarLat;
double s_lon = config::kDefaultRadarLon;

bool parseCoord(const char* text, double* out) {
  if (text == nullptr || text[0] == '\0') {
    return false;
  }
  char* end = nullptr;
  const double v = strtod(text, &end);
  if (end == text || (end != nullptr && *end != '\0')) {
    return false;
  }
  *out = v;
  return true;
}

bool validLatLon(double lat, double lon) {
  return lat >= -90.0 && lat <= 90.0 && lon >= -180.0 && lon <= 180.0;
}

int32_t toE7(double degrees) {
  return static_cast<int32_t>(lround(degrees * 1e7));
}

double fromE7(int32_t e7) { return static_cast<double>(e7) * 1e-7; }

bool persist(double lat, double lon) {
  Preferences prefs;
  if (!prefs.begin(kPrefsNamespace, false)) {
    Serial.println("location: NVS open failed — coordinates not saved");
    return false;
  }
  const int32_t lat_e7 = toE7(lat);
  const int32_t lon_e7 = toE7(lon);
  const bool wrote =
      prefs.putLong(kKeyLatE7, lat_e7) == sizeof(lat_e7) &&
      prefs.putLong(kKeyLonE7, lon_e7) == sizeof(lon_e7);
  // Drop legacy blob keys so future boots use the integer path only.
  prefs.remove(kKeyLat);
  prefs.remove(kKeyLon);
  prefs.end();
  if (!wrote) {
    Serial.println("location: NVS write failed — coordinates not saved");
    return false;
  }
  s_lat = lat;
  s_lon = lon;
  return true;
}

}  // namespace

void init() {
  Preferences prefs;
  if (!prefs.begin(kPrefsNamespace, true)) {
    Serial.println("location: no saved coordinates (using defaults)");
    return;
  }

  if (prefs.isKey(kKeyLatE7) && prefs.isKey(kKeyLonE7)) {
    const double lat = fromE7(prefs.getLong(kKeyLatE7, toE7(config::kDefaultRadarLat)));
    const double lon = fromE7(prefs.getLong(kKeyLonE7, toE7(config::kDefaultRadarLon)));
    prefs.end();
    if (validLatLon(lat, lon)) {
      s_lat = lat;
      s_lon = lon;
      Serial.printf("location: restored %.6f, %.6f\n", s_lat, s_lon);
    }
    return;
  }

  if (prefs.isKey(kKeyLat) && prefs.isKey(kKeyLon)) {
    const double lat = prefs.getDouble(kKeyLat, config::kDefaultRadarLat);
    const double lon = prefs.getDouble(kKeyLon, config::kDefaultRadarLon);
    prefs.end();
    if (validLatLon(lat, lon)) {
      Serial.printf("location: migrating legacy doubles %.6f, %.6f\n", lat, lon);
      if (!persist(lat, lon)) {
        s_lat = lat;
        s_lon = lon;
      }
    }
    return;
  }

  prefs.end();
}

double lat() { return s_lat; }

double lon() { return s_lon; }

bool save(double lat, double lon) {
  if (!validLatLon(lat, lon)) {
    return false;
  }
  if (!persist(lat, lon)) {
    return false;
  }
  Serial.printf("Radar location saved: %.6f, %.6f\n", lat, lon);
  return true;
}

bool saveFromStrings(const char* lat_str, const char* lon_str) {
  double lat = 0.0;
  double lon = 0.0;
  if (!parseCoord(lat_str, &lat) || !parseCoord(lon_str, &lon)) {
    return false;
  }
  return save(lat, lon);
}

void clear() {
  Preferences prefs;
  if (prefs.begin(kPrefsNamespace, false)) {
    prefs.remove(kKeyLat);
    prefs.remove(kKeyLon);
    prefs.remove(kKeyLatE7);
    prefs.remove(kKeyLonE7);
    prefs.end();
  }
  s_lat = config::kDefaultRadarLat;
  s_lon = config::kDefaultRadarLon;
}

}  // namespace services::location
