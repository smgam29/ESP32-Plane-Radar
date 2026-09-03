#pragma once

// Override from PlatformIO/CI with:
//   -DPLANE_RADAR_VERSION=\"1.2.0\"
#ifndef PLANE_RADAR_VERSION
#define PLANE_RADAR_VERSION "1.8.1-dev"
#endif

namespace firmware {

constexpr char kVersion[] = PLANE_RADAR_VERSION;

}  // namespace firmware
