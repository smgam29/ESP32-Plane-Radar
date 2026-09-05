#pragma once

#include <cstddef>
#include "services/aircraft_labels.h"

namespace services::adsb {

struct Aircraft {
  float lat;
  float lon;
  float nose_deg;
  float track_deg;
  float gs_knots;
  AircraftLabelData labels;
};

constexpr size_t kMaxAircraft = 64;

size_t aircraftCount();
const Aircraft* aircraftList();

struct Query {
  double lat;
  double lon;
  float radius_km;
};
inline bool sameQuery(const Query& a, const Query& b) {
  return a.lat == b.lat && a.lon == b.lon &&
         a.radius_km == b.radius_km;
}

/** Main-loop-only API: one queued/in-flight request maximum. */
bool begin();
bool requestUpdate(const Query& query);
bool applyUpdate(const Query& current);
bool busy();
uint32_t completedUpdates();
uint32_t workerStackFree();
/** OTA drains a pending request before flash writes; timeout leaves firmware intact. */
bool waitForIdle(uint32_t timeout_ms);

}  // namespace services::adsb
