#include "services/web_portal.h"

#include <Arduino.h>
#include <Update.h>
#include <WiFi.h>
#include <WiFiManager.h>

#include <esp_system.h>

#include <cstdio>
#include <cstring>

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
section{background:#102536;border:1px solid #24485a;border-radius:.8rem;padding:1rem 1.2rem;margin-bottom:1rem}
h2{font-size:1.05rem;margin:.1rem 0 1rem}dl{display:grid;grid-template-columns:1fr 1fr;gap:.7rem;margin:0}
dt{color:#9ab6c2}dd{margin:0;text-align:right;overflow-wrap:anywhere}nav{margin-top:1rem}
a{color:#5ee6a8;margin-right:1rem}small{display:block;color:#9ab6c2;margin:.7rem 0}
button{background:#5ee6a8;border:0;border-radius:.4rem;color:#071421;font-weight:700;padding:.65rem 1rem}
input{box-sizing:border-box;max-width:100%;margin-bottom:.8rem}label{display:block;color:#9ab6c2}
.coords{display:grid;grid-template-columns:1fr 1fr;gap:.8rem}.coords input{width:100%;padding:.5rem}
.secondary{background:#24485a;color:#eef7f4;margin-right:.5rem}progress{display:none;width:100%;margin-top:1rem}
.message{min-height:1.4rem}.error{color:#ff9b9b}.success{color:#5ee6a8}
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
<dt>Latitude</dt><dd id="status-lat">...</dd>
<dt>Longitude</dt><dd id="status-lon">...</dd>
</dl>
</section>
<section>
<h2>Radar location</h2>
<form id="location-form">
<div class="coords">
<label>Latitude<input id="lat" name="lat" type="number" min="-90" max="90" step="0.000001" inputmode="decimal" required></label>
<label>Longitude<input id="lon" name="lon" type="number" min="-180" max="180" step="0.000001" inputmode="decimal" required></label>
</div>
<button id="locate" class="secondary" type="button">Use my current location</button>
<button id="save-location" type="submit">Save location</button>
</form>
<p id="location-message" class="message" role="status"></p>
<small>New coordinates apply to the next aircraft refresh.</small>
</section>
<section>
<h2>Firmware</h2>
<form id="update-form">
<input id="firmware" name="firmware" type="file" accept=".bin,application/octet-stream" required>
<br><button id="install" type="submit">Install update</button>
</form>
<progress id="progress" max="100" value="0"></progress>
<p id="message" class="message" role="status"></p>
<small>Choose the application <strong>firmware.bin</strong>, not a merged USB image.</small>
</section>
<nav><a href="/wifi">Wi-Fi setup</a><a href="/param">Radar settings</a></nav>
<script>
const lat=document.getElementById('lat'),lon=document.getElementById('lon'),locMessage=document.getElementById('location-message');
function showLocationMessage(text,ok=false){locMessage.textContent=text;locMessage.className='message '+(ok?'success':'error')}
function loadStatus(){return fetch('/api/status',{cache:'no-store'}).then(r=>{if(!r.ok)throw Error();return r.json()}).then(s=>{
for(const k of ['version','wifi','ip'])document.getElementById(k).textContent=s[k];
document.getElementById('status-lat').textContent=s.lat;document.getElementById('status-lon').textContent=s.lon;
lat.value=s.lat;lon.value=s.lon
})}
loadStatus().catch(()=>document.getElementById('wifi').textContent='Status unavailable');
document.getElementById('location-form').addEventListener('submit',e=>{e.preventDefault();
if(!e.target.reportValidity())return;const save=document.getElementById('save-location');save.disabled=true;showLocationMessage('Saving...');
fetch('/api/settings/location',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams({lat:lat.value,lon:lon.value})})
.then(async r=>{const result=await r.json();if(!r.ok)throw Error(result.message||'Location was not saved.');
document.getElementById('status-lat').textContent=result.lat;document.getElementById('status-lon').textContent=result.lon;
lat.value=result.lat;lon.value=result.lon;showLocationMessage(result.message,true)
}).catch(e=>showLocationMessage(e.message||'Location was not saved.')).finally(()=>save.disabled=false)
});
document.getElementById('locate').addEventListener('click',()=>{
if(!navigator.geolocation){showLocationMessage('Location is not available in this browser.');return}
showLocationMessage('Requesting your location...');navigator.geolocation.getCurrentPosition(p=>{
lat.value=p.coords.latitude.toFixed(6);lon.value=p.coords.longitude.toFixed(6);showLocationMessage('Location filled in. Select Save location to apply it.',true)
},e=>showLocationMessage(e.message||'Could not get your location.'),{enableHighAccuracy:true,timeout:15000,maximumAge:60000})
});
const form=document.getElementById('update-form'),file=document.getElementById('firmware'),
button=document.getElementById('install'),bar=document.getElementById('progress'),message=document.getElementById('message');
form.addEventListener('submit',e=>{e.preventDefault();const f=file.files[0];
if(!f||!f.name.toLowerCase().endsWith('.bin')){message.textContent='Choose a firmware .bin file.';return}
button.disabled=true;bar.style.display='block';bar.value=0;message.textContent='Uploading...';
const data=new FormData();data.append('firmware',f);const request=new XMLHttpRequest();
request.open('POST','/api/update');request.upload.onprogress=e=>{if(e.lengthComputable)bar.value=e.loaded/e.total*100};
request.onload=()=>{let result;try{result=JSON.parse(request.responseText)}catch(e){result={ok:false,message:'Invalid response from device.'}}
message.textContent=result.message;if(!result.ok)button.disabled=false};
request.onerror=()=>{message.textContent='Upload connection failed. Current firmware was not replaced.';button.disabled=false};
request.send(data)})
</script>
</body>
</html>)HTML";

constexpr size_t kAppDescriptorOffset = 0x20;
constexpr uint8_t kAppDescriptorMagic[] = {0x32, 0x54, 0xcd, 0xab};

bool s_update_in_progress = false;
bool s_update_started = false;
bool s_update_failed = false;
bool s_reboot_pending = false;
unsigned long s_reboot_at_ms = 0;
char s_update_message[128] = "No firmware upload received.";

void setUpdateFailure(const char* message) {
  s_update_failed = true;
  snprintf(s_update_message, sizeof(s_update_message), "%s", message);
}

bool isApplicationImage(const uint8_t* data, size_t size) {
  if (size < kAppDescriptorOffset + sizeof(kAppDescriptorMagic) || data[0] != 0xe9) {
    return false;
  }
  return memcmp(data + kAppDescriptorOffset, kAppDescriptorMagic,
                sizeof(kAppDescriptorMagic)) == 0;
}

void handleUpdateUpload(WiFiManager& wifi_manager) {
  HTTPUpload& upload = wifi_manager.server->upload();
  if (upload.status == UPLOAD_FILE_START) {
    s_update_in_progress = true;
    s_update_started = false;
    s_update_failed = false;
    s_reboot_pending = false;
    s_update_message[0] = '\0';
    if (wifi_manager.getConfigPortalActive()) {
      setUpdateFailure("Finish Wi-Fi setup before installing firmware.");
      return;
    }
    String filename = upload.filename;
    filename.toLowerCase();
    if (!filename.endsWith(".bin")) {
      setUpdateFailure("Choose an application firmware .bin file.");
    }
    return;
  }

  if (upload.status == UPLOAD_FILE_WRITE) {
    if (s_update_failed) {
      return;
    }
    if (!s_update_started) {
      if (!isApplicationImage(upload.buf, upload.currentSize)) {
        setUpdateFailure("Invalid application image. Do not upload the merged USB image.");
        return;
      }
      if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) {
        setUpdateFailure(Update.errorString());
        return;
      }
      s_update_started = true;
    }
    if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
      setUpdateFailure(Update.errorString());
    }
    return;
  }

  if (upload.status == UPLOAD_FILE_END) {
    if (!s_update_failed && !s_update_started) {
      setUpdateFailure("The uploaded firmware file was empty.");
    }
    if (!s_update_failed && !Update.end(true)) {
      setUpdateFailure(Update.errorString());
    }
    if (s_update_failed) {
      if (s_update_started && Update.isRunning()) {
        Update.abort();
      }
      s_update_in_progress = false;
      return;
    }
    snprintf(s_update_message, sizeof(s_update_message),
             "Update installed. Rebooting...");
    s_reboot_pending = true;
    s_reboot_at_ms = millis() + 1500UL;
    return;
  }

  if (upload.status == UPLOAD_FILE_ABORTED) {
    if (s_update_started && Update.isRunning()) {
      Update.abort();
    }
    setUpdateFailure("Upload was cancelled. Current firmware was not replaced.");
    s_update_in_progress = false;
  }
}

void sendUpdateResult(WebServer& server) {
  char response[192];
  const bool ok = !s_update_failed && s_reboot_pending;
  snprintf(response, sizeof(response), "{\"ok\":%s,\"message\":\"%s\"}",
           ok ? "true" : "false", s_update_message);
  server.sendHeader("Cache-Control", "no-store");
  server.send(ok ? 200 : 400, "application/json", response);
}

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

void sendLocationResult(WebServer& server) {
  if (s_update_in_progress) {
    server.send(409, "application/json",
                "{\"ok\":false,\"message\":\"Wait for the firmware update to finish.\"}");
    return;
  }
  if (!server.hasArg("lat") || !server.hasArg("lon") ||
      !services::location::saveFromStrings(server.arg("lat").c_str(),
                                           server.arg("lon").c_str())) {
    server.send(400, "application/json",
                "{\"ok\":false,\"message\":\"Enter latitude from -90 to 90 and longitude from -180 to 180.\"}");
    return;
  }

  char response[160];
  snprintf(response, sizeof(response),
           "{\"ok\":true,\"message\":\"Location saved.\","
           "\"lat\":\"%.6f\",\"lon\":\"%.6f\"}",
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
    manager->server->on("/api/settings/location", HTTP_POST,
                        [manager]() { sendLocationResult(*manager->server); });
    manager->server->on(
        "/api/update", HTTP_POST,
        [manager]() { sendUpdateResult(*manager->server); },
        [manager]() { handleUpdateUpload(*manager); });
  });
}

bool updateInProgress() { return s_update_in_progress; }

void loop() {
  if (s_reboot_pending &&
      static_cast<long>(millis() - s_reboot_at_ms) >= 0) {
    esp_restart();
  }
}

}  // namespace services::web
