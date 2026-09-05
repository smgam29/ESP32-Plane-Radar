/**
 * Plane Radar — WiFi setup, then radar UI on the round GC9A01 display.
 */

#include <Arduino.h>
#include <WiFi.h>
#include <esp_mac.h>

#include "config.h"
#include "hardware/display.h"
#include "services/adsb_client.h"
#include "services/radar_location.h"
#include "services/web_portal.h"
#include "services/wifi_setup.h"
#include "ui/radar_display.h"
#include "ui/radar_range.h"
#include "ui/status_screens.h"
#include "version.h"

namespace {

bool g_radar_visible = false;
bool g_identity_visible = false;
unsigned long g_identity_until_ms = 0;
unsigned long g_wifi_down_since = 0;
unsigned long g_last_reconnect_ms = 0;
unsigned long g_last_adsb_fetch_ms = 0;

void pollBackground() {
  wifiLoop();
  if (g_identity_visible) {
    if (static_cast<long>(millis() - g_identity_until_ms) < 0) return;
    g_identity_visible = false;
    if (g_radar_visible && WiFi.status() == WL_CONNECTED) {
      ui::radarDisplayDraw();
    }
  }
  if (g_radar_visible && WiFi.status() == WL_CONNECTED &&
      !services::web::updateInProgress()) {
    if (services::web::consumeDisplayRefreshRequest()) {
      ui::radarDisplayDraw();
    }
    ui::radarDisplayAnimate();
  }
}

void showRadarIfConnected() {
  if (WiFi.status() != WL_CONNECTED) {
    g_radar_visible = false;
    return;
  }
  ui::radarDisplayDraw();
  g_radar_visible = true;
}

void onRangeTap() {
  ui::radar::rangeNext();
  char range_label[12];
  ui::radar::formatCurrentRing3Label(range_label, sizeof(range_label));
  Serial.printf("Range: %s (outer ~%.0f km)\n", range_label,
                ui::radar::rangeCurrent().outer_km);

  if (g_radar_visible && !g_identity_visible &&
      WiFi.status() == WL_CONNECTED) {
    ui::radarDisplayDraw();
  }
}

void showDeviceIdentity() {
  uint8_t mac[6] = {};
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
  char device_id[5];
  snprintf(device_id, sizeof(device_id), "%02X%02X", mac[4], mac[5]);
  const String ip = WiFi.status() == WL_CONNECTED
                        ? WiFi.localIP().toString()
                        : String("Not connected");
  statusScreenDeviceIdentity(device_id, ip.c_str());
  g_identity_visible = true;
  g_identity_until_ms = millis() + config::kIdentityScreenMs;
  Serial.printf("Device identity: Plane Radar %s, IP %s\n", device_id,
                ip.c_str());
}

void handleBootButton() {
  static uint8_t rapid_taps = 0;
  static unsigned long last_tap_ms = 0;
  bootButtonPollLongPress();
  if (bootButtonConsumeTap()) {
    const unsigned long now = millis();
    rapid_taps = rapid_taps > 0 && now - last_tap_ms <= config::kIdentityTapGapMs
                     ? rapid_taps + 1
                     : 1;
    last_tap_ms = now;
    if (rapid_taps >= 5) {
      rapid_taps = 0;
      showDeviceIdentity();
      return;
    }
    onRangeTap();
  }
}

services::adsb::Query currentQuery() {
  return {services::location::lat(), services::location::lon(),
          ui::radar::fetchRadiusKm()};
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println();
  Serial.println("Plane Radar");
  Serial.printf("Firmware: %s\n", firmware::kVersion);

  bootButtonInit();
  displayInit();
  if (wifiShowsSetupScreenOnBoot()) {
    statusScreenPortal(wifiPortalHostUrl());
  }
  services::location::init();
  ui::radar::rangeInit();


  if (wifiSetupConnect()) {
    showRadarIfConnected();
    services::adsb::begin();
  }
}

void loop() {
  handleBootButton();
  pollBackground();
  const bool new_aircraft = services::adsb::applyUpdate(currentQuery());
  if (new_aircraft && g_radar_visible && WiFi.status() == WL_CONNECTED &&
      !g_identity_visible && !services::web::updateInProgress()) {
    ui::radarDisplayRefreshAircraft();
  }

  if (WiFi.status() != WL_CONNECTED) {
    if (g_radar_visible) {
      Serial.println("WiFi lost — will reconnect");
      g_radar_visible = false;
    }

    if (g_wifi_down_since == 0) {
      g_wifi_down_since = millis();
    }

    const unsigned long down_ms = millis() - g_wifi_down_since;
    if (down_ms >= config::kWifiDownGraceMs &&
        millis() - g_last_reconnect_ms >= config::kWifiReconnectIntervalMs) {
      g_last_reconnect_ms = millis();
      if (wifiReconnect()) {
        g_wifi_down_since = 0;
        showRadarIfConnected();
      }
    }
  } else {
    g_wifi_down_since = 0;
    if (!g_radar_visible) {
      showRadarIfConnected();
    } else if (!services::web::updateInProgress() &&
               millis() - g_last_adsb_fetch_ms >= config::kAdsbFetchIntervalMs) {
      // Start at most one request; captures settings before handing off.
      if (services::adsb::begin() && services::adsb::requestUpdate(currentQuery()))
        g_last_adsb_fetch_ms = millis();
    }
  }

  delay(10);
}
