#pragma once

class WiFiManager;

namespace services::web {

/** Register Plane Radar routes on WiFiManager's existing port-80 server. */
void attach(WiFiManager& wifi_manager);

}  // namespace services::web
