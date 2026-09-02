#include "services/web_portal.h"

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiManager.h>

#include "services/radar_location.h"
#include "version.h"

namespace services::web {

namespace {

const char kStatusPage[] PROGMEM = R"HTML(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Plane Radar</title>
<style>
:root{color-scheme:dark;font-family:system-ui,sans-serif;background:#071421;color:#eef7f4}
body{max-width:34rem;margin:auto;padding:1.5rem}h1{color:#5ee6a8;margin-bottom:1.5rem}
section{background:#102536;border:1px solid #24485a;border-radius:.8rem;padding:1rem 1.2rem}
h2{font-size:1.05rem;margin:.1rem 0 1rem}dl{display:grid;grid-template-columns:1fr 1fr;gap:.7rem;margin:0}
dt{color:#9ab6c2}dd{margin:0;text-align:right;overflow-wrap:anywhere}nav{margin-top:1rem}
a{color:#5ee6a8;margin-right:1rem}small{display:block;color:#9ab6c2;margin-top:1.5rem}
</style>
</head>
<body>
<h1>Plane Radar</h1>
<section>
<h2>Status</h2>
<dl>
<dt>Firmware version</dt><dd id="version">...</dd>
<dt>Wi-Fi</dt><dd id="wifi">...</dd>
<dt>IP address</dt><dd id="ip">...</dd>
<dt>Latitude</dt><dd id="lat">...</dd>
<dt>Longitude</dt><dd id="lon">...</dd>
</dl>
</section>
<nav><a href="/wifi">Wi-Fi setup</a><a href="/param">Radar settings</a></nav>
<small>Updates will be available here in a later firmware version.</small>
<script>
fetch('/api/status',{cache:'no-store'}).then(r=>{if(!r.ok)throw Error();return r.json()}).then(s=>{
for(const k of ['version','wifi','ip','lat','lon'])document.getElementById(k).textContent=s[k]
}).catch(()=>document.getElementById('wifi').textContent='Status unavailable')
</script>
</body>
</html>)HTML";

void sendStatus(WebServer& server) {
  const bool connected = WiFi.status() == WL_CONNECTED;
  const String ip = connected ? WiFi.localIP().toString() : String("Unavailable");
  char response[256];
  snprintf(response, sizeof(response),
           "{\"version\":\"%s\",\"wifi\":\"%s\",\"ip\":\"%s\","
           "\"lat\":\"%.6f\",\"lon\":\"%.6f\"}",
           firmware::kVersion, connected ? "Connected" : "Disconnected", ip.c_str(),
           services::location::lat(), services::location::lon());
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", response);
}

void sendHome(WiFiManager& wifi_manager) {
  WebServer& server = *wifi_manager.server;
  if (wifi_manager.getConfigPortalActive()) {
    server.sendHeader("Location", "/wifi", true);
    server.send(302, "text/plain", "");
    return;
  }
  server.sendHeader("Cache-Control", "no-store");
  server.send_P(200, "text/html", kStatusPage);
}

}  // namespace

void attach(WiFiManager& wifi_manager) {
  WiFiManager* const manager = &wifi_manager;
  wifi_manager.setWebServerCallback([manager]() {
    manager->server->on("/", [manager]() { sendHome(*manager); });
    manager->server->on("/api/status",
                        [manager]() { sendStatus(*manager->server); });
  });
}

}  // namespace services::web
