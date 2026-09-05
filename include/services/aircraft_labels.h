#pragma once

#include <cstddef>
#include <cstdint>

namespace services::adsb {
enum class Label : uint8_t {
  Callsign, AircraftType, Altitude, Registration, GroundSpeed,
  VerticalRate, Squawk, Category, Navigation, Military, Count
};
constexpr uint8_t kLabelCount = static_cast<uint8_t>(Label::Count);
constexpr uint8_t kMaxLabels = 3;
constexpr uint16_t kDefaultLabelMask = 7;
constexpr uint16_t kAllLabelMask = (1U << kLabelCount) - 1;
constexpr uint16_t labelBit(Label label) {
  return 1U << static_cast<uint8_t>(label);
}
inline bool validLabelMask(uint16_t mask) {
  if (mask & ~kAllLabelMask) return false;
  unsigned count = 0;
  for (; mask; mask >>= 1) count += mask & 1;
  return count <= kMaxLabels;
}
struct AircraftLabel {
  Label kind;
  char text[24];
};
struct AircraftLabels {
  AircraftLabel lines[kMaxLabels];
  uint8_t count;
  bool emergency;
};
// Fixed-size, selection-independent snapshot. No JSON or heap ownership crosses
// from the fetch worker to the display; missing numbers are represented by NaN.
struct AircraftLabelData {
  float altitude;
  float groundSpeed;
  float verticalRate;
  char callsign[24];
  char aircraftType[8];
  char registration[16];
  char squawk[8];
  char category[4];
  uint8_t navigation;
  bool ground;
  bool military;
  bool emergency;
};
static_assert(sizeof(AircraftLabelData) <= 80, "Keep aircraft snapshots small");
void formatAircraftLabels(const AircraftLabelData& data, uint16_t mask,
                          AircraftLabels& out);
}  // namespace services::adsb
