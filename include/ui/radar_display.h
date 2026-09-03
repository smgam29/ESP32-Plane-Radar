#pragma once

namespace ui {

/** Draw the static sonar/radar grid (black disc, green overlay, labels). */
void radarDisplayDraw();
/** Rate-limited decorative sweep; uses the existing framebuffer only. */
void radarDisplayAnimate();

/** Redraw aircraft only (blits cached grid; no full-screen clear). */
void radarDisplayRefreshAircraft();

}  // namespace ui
