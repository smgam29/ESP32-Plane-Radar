#pragma once
#include <cstdint>

namespace ui {

/** Draw the static sonar/radar grid (black disc, green overlay, labels). */
void radarDisplayDraw();
/** Rate-limited decorative sweep; uses the existing framebuffer only. */
void radarDisplayAnimate();
uint32_t sweepFrameCount();
uint32_t sweepMaxGapMs();

/** Redraw the frame, or let the active sweep draw the new aircraft next frame. */
void radarDisplayRefreshAircraft();

}  // namespace ui
