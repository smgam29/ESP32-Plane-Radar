#pragma once

class WiFiManager;

namespace services::web {

/** Register Plane Radar routes on WiFiManager's existing port-80 server. */
void attach(WiFiManager& wifi_manager);
/** True from upload start until failure cleanup or successful reboot. */
bool updateInProgress();
/** Consume a one-shot redraw request raised by a successful settings save. */
bool consumeDisplayRefreshRequest();
/** Complete deferred actions such as rebooting after the HTTP response is sent. */
void loop();

}  // namespace services::web
