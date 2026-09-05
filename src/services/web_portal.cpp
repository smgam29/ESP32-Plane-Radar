#include "services/web_portal.h"

#include <Arduino.h>
#include <Update.h>
#include <WiFi.h>
#include <WiFiManager.h>

#include <esp_system.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "data/large_airports.h"
#include "services/radar_location.h"
#include "services/aircraft_labels.h"
#include "services/adsb_client.h"
#include "ui/radar_display.h"
#include "services/wifi_setup.h"
#include "ui/radar_range.h"
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
button,.button{background:#5ee6a8;border:0;border-radius:.4rem;color:#071421;display:inline-block;font-weight:700;padding:.65rem 1rem;text-decoration:none}
input,select{box-sizing:border-box;max-width:100%;margin-bottom:.8rem}label{display:block;color:#9ab6c2}
.coords{display:grid;grid-template-columns:1fr 1fr;gap:.8rem}.coords input{width:100%;padding:.5rem}
.checks{display:flex;flex-wrap:wrap;gap:.5rem 1rem;margin-bottom:.8rem}.checks label{color:#eef7f4}.checks input{margin:0 .3rem 0 0}
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
<dt>Device address</dt><dd id="hostname">...</dd>
<dt>Latitude</dt><dd id="status-lat">...</dd>
<dt>Longitude</dt><dd id="status-lon">...</dd>
</dl>
</section>
<section>
<h2>Radar orientation</h2>
<form id="orientation-form">
<label>Top of radar
<select id="orientation" name="orientation">
<option value="N">North (N)</option><option value="E">East (E)</option>
<option value="S">South (S)</option><option value="W">West (W)</option>
</select></label>
<button id="save-orientation" type="submit">Save orientation</button>
</form>
<p id="orientation-message" class="message" role="status"></p>
</section>
<section>
<h2>Radar appearance</h2>
<form id="appearance-form">
<div class="checks"><label><input id="dim-rings" type="checkbox">Dim radar rings by 50%</label></div>
<button id="save-appearance" type="submit" disabled>Save appearance</button>
</form>
<p id="appearance-message" class="message" role="status"></p>
<form id="sweep-form">
<div class="checks"><label><input id="sweep" type="checkbox">Show decorative radar sweep</label></div>
<button id="save-sweep" type="submit" disabled>Save sweep</button>
</form>
<p id="sweep-message" class="message" role="status"></p>
<small>Thin line, five seconds per rotation. Aircraft updates are independent. Network requests run separately from the animation.</small>
<small>Softens only the rings. Aircraft, labels, runways and crosshairs stay unchanged. Applies on the next radar refresh.</small>
</section>
<section>
<h2>Plane labels</h2>
<form id="labels-form">
<div class="checks">
<label><input id="label-0" class="plane-label" type="checkbox" value="1">Callsign</label>
<label><input id="label-1" class="plane-label" type="checkbox" value="2">Aircraft type</label>
<label><input id="label-2" class="plane-label" type="checkbox" value="4">Altitude</label>
<label><input id="label-3" class="plane-label" type="checkbox" value="8">Registration</label>
<label><input id="label-4" class="plane-label" type="checkbox" value="16">Ground speed</label>
<label><input id="label-5" class="plane-label" type="checkbox" value="32">Climb/descent rate</label>
<label><input id="label-6" class="plane-label" type="checkbox" value="64">Squawk</label>
<label><input id="label-7" class="plane-label" type="checkbox" value="128">Aircraft category</label>
<label><input id="label-8" class="plane-label" type="checkbox" value="256">Navigation modes</label>
<label><input id="label-9" class="plane-label" type="checkbox" value="512">Military marker</label>
</div>
<p id="label-count" role="status">Choose up to 3 labels.</p>
<button id="save-labels" type="submit" disabled>Save labels</button>
</form>
<p id="labels-message" class="message" role="status"></p>
<small>Choose up to three; unavailable fields are omitted. Changes apply on the next aircraft refresh. Speed is in knots; vertical rate is in ft/min. Category uses its ADS-B code. Navigation modes use AP, ALT, LNAV, VNAV, APP and TCAS (+ means more). MIL appears only when flagged. Reported emergencies turn the icon and labels orange-red, independent of these choices.</small>
</section>
<section>
<h2>Airports</h2>
<form id="airports-form">
<div class="checks">
<label><input id="airport-runways" type="checkbox">Runway layout</label>
<label><input id="airport-labels" type="checkbox">ICAO labels</label>
</div>
<button id="save-airports" type="submit">Save airports</button>
</form>
<p id="airports-message" class="message" role="status"></p>
<small>Runway patterns cover worldwide major airports and UK fixed-wing airfields.</small>
</section>
<section>
<h2>Radar location</h2>
<form id="airport-location-form">
<label>Set base from ICAO airport
<input id="airport-icao" list="airport-options" maxlength="4" autocapitalize="characters" autocomplete="off" placeholder="EGHI" pattern="[A-Za-z0-9]{4}" required></label>
<datalist id="airport-options"></datalist>
<button id="save-airport-location" type="submit">Use airport</button>
</form>
<p id="airport-location-message" class="message" role="status"></p>
<small>Enter a four-letter ICAO code. Suggestions cover worldwide major airports and UK fixed-wing airfields.</small>
<form id="location-form">
<div class="coords">
<label>Latitude<input id="lat" name="lat" type="number" min="-90" max="90" step="0.000001" inputmode="decimal" required></label>
<label>Longitude<input id="lon" name="lon" type="number" min="-180" max="180" step="0.000001" inputmode="decimal" required></label>
</div>
<a class="button secondary" href="https://www.latlong.net/" target="_blank" rel="noopener noreferrer">Find coordinates on map</a>
<button id="save-location" type="submit">Save location</button>
</form>
<p id="location-message" class="message" role="status"></p>
<small>Search for a place or select a point on the map, then copy its coordinates here. New coordinates apply to the next aircraft refresh.</small>
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
const airportIcao=document.getElementById('airport-icao'),airportOptions=document.getElementById('airport-options');
const labelBoxes=[...document.querySelectorAll('.plane-label')];
let labelsLoaded=false;
function syncLabelLimit(){
const count=labelBoxes.filter(b=>b.checked).length;
labelBoxes.forEach(b=>b.disabled=!labelsLoaded||(!b.checked&&count>=3));
document.getElementById('label-count').textContent=count+'/3 labels selected'+(count>=3?' — deselect one to choose another.':'.');
document.getElementById('save-labels').disabled=!labelsLoaded;
}
labelBoxes.forEach(b=>b.addEventListener('change',syncLabelLimit));
syncLabelLimit();
function showMessage(el,text,ok=false){el.textContent=text;el.className='message '+(ok?'success':'error')}
function loadStatus(){return fetch('/api/status',{cache:'no-store'}).then(r=>{if(!r.ok)throw Error();return r.json()}).then(s=>{
for(const k of ['version','wifi','ip','hostname'])document.getElementById(k).textContent=s[k];
document.getElementById('status-lat').textContent=s.lat;document.getElementById('status-lon').textContent=s.lon;
lat.value=s.lat;lon.value=s.lon;document.getElementById('orientation').value=s.orientation;
document.getElementById('dim-rings').checked=s.dimRings;
document.getElementById('save-appearance').disabled=false;
document.getElementById('sweep').checked=s.sweep;
document.getElementById('save-sweep').disabled=false;
labelBoxes.forEach(b=>b.checked=(s.labelMask&Number(b.value))!==0);labelsLoaded=true;syncLabelLimit();
document.getElementById('airport-runways').checked=s.airportRunways;
document.getElementById('airport-labels').checked=s.airportLabels
})}
loadStatus().catch(()=>document.getElementById('wifi').textContent='Status unavailable');
airportIcao.addEventListener('input',()=>{const query=airportIcao.value.trim().toUpperCase();airportIcao.value=query;
if(query.length<2){airportOptions.replaceChildren();return}
fetch('/api/airports?query='+encodeURIComponent(query),{cache:'no-store'}).then(r=>r.json()).then(result=>{
airportOptions.replaceChildren(...result.airports.map(code=>{const option=document.createElement('option');option.value=code;return option}))
}).catch(()=>airportOptions.replaceChildren())
});
document.getElementById('airport-location-form').addEventListener('submit',e=>{e.preventDefault();
if(!e.target.reportValidity())return;const save=document.getElementById('save-airport-location'),msg=document.getElementById('airport-location-message');save.disabled=true;showMessage(msg,'Saving...');
fetch('/api/settings/airport-location',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams({icao:airportIcao.value.trim().toUpperCase()})})
.then(async r=>{const result=await r.json();if(!r.ok)throw Error(result.message||'Airport was not found.');
airportIcao.value=result.icao;lat.value=result.lat;lon.value=result.lon;document.getElementById('status-lat').textContent=result.lat;document.getElementById('status-lon').textContent=result.lon;showMessage(msg,result.message,true)
}).catch(e=>showMessage(msg,e.message||'Airport was not found.')).finally(()=>save.disabled=false)
});
document.getElementById('orientation-form').addEventListener('submit',e=>{e.preventDefault();
const save=document.getElementById('save-orientation'),msg=document.getElementById('orientation-message');save.disabled=true;showMessage(msg,'Saving...');
fetch('/api/settings/orientation',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams({orientation:document.getElementById('orientation').value})})
.then(async r=>{const result=await r.json();if(!r.ok)throw Error(result.message||'Orientation was not saved.');showMessage(msg,result.message,true)
}).catch(e=>showMessage(msg,e.message||'Orientation was not saved.')).finally(()=>save.disabled=false)
});
document.getElementById('airports-form').addEventListener('submit',e=>{e.preventDefault();
const save=document.getElementById('save-airports'),msg=document.getElementById('airports-message');save.disabled=true;showMessage(msg,'Saving...');
const checked=id=>document.getElementById(id).checked?'1':'0';
fetch('/api/settings/airports',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams({runways:checked('airport-runways'),labels:checked('airport-labels')})})
.then(async r=>{const result=await r.json();if(!r.ok)throw Error(result.message||'Airport settings were not saved.');showMessage(msg,result.message,true)
}).catch(e=>showMessage(msg,e.message||'Airport settings were not saved.')).finally(()=>save.disabled=false)
});
document.getElementById('sweep-form').addEventListener('submit',e=>{e.preventDefault();
const save=document.getElementById('save-sweep'),msg=document.getElementById('sweep-message');
save.disabled=true;showMessage(msg,'Saving...');
fetch('/api/settings/sweep',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams({enabled:document.getElementById('sweep').checked?'1':'0'})})
.then(async r=>{const result=await r.json();if(!r.ok)throw Error(result.message||'Sweep was not saved.');showMessage(msg,result.message,true)
}).catch(e=>showMessage(msg,e.message||'Sweep was not saved.')).finally(()=>save.disabled=false)
});
document.getElementById('appearance-form').addEventListener('submit',e=>{e.preventDefault();
const save=document.getElementById('save-appearance'),msg=document.getElementById('appearance-message');
save.disabled=true;showMessage(msg,'Saving...');
fetch('/api/settings/appearance',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams({dimRings:document.getElementById('dim-rings').checked?'1':'0'})})
.then(async r=>{const result=await r.json();if(!r.ok)throw Error(result.message||'Appearance was not saved.');showMessage(msg,result.message,true)
}).catch(e=>showMessage(msg,e.message||'Appearance was not saved.')).finally(()=>save.disabled=false)
});
document.getElementById('labels-form').addEventListener('submit',e=>{e.preventDefault();
const save=document.getElementById('save-labels'),msg=document.getElementById('labels-message');
if(!labelsLoaded||labelBoxes.filter(b=>b.checked).length>3){showMessage(msg,'Choose at most three labels.');return}
const mask=labelBoxes.reduce((m,b)=>b.checked?m|Number(b.value):m,0);
save.disabled=true;showMessage(msg,'Saving...');
fetch('/api/settings/labels',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams({mask:String(mask)})})
.then(async r=>{const result=await r.json();if(!r.ok)throw Error(result.message||'Labels were not saved.');showMessage(msg,result.message,true)
}).catch(e=>showMessage(msg,e.message||'Labels were not saved.')).finally(()=>save.disabled=false)
});
document.getElementById('location-form').addEventListener('submit',e=>{e.preventDefault();
if(!e.target.reportValidity())return;const save=document.getElementById('save-location');save.disabled=true;showMessage(locMessage,'Saving...');
fetch('/api/settings/location',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams({lat:lat.value,lon:lon.value})})
.then(async r=>{const result=await r.json();if(!r.ok)throw Error(result.message||'Location was not saved.');
document.getElementById('status-lat').textContent=result.lat;document.getElementById('status-lon').textContent=result.lon;
lat.value=result.lat;lon.value=result.lon;showMessage(locMessage,result.message,true)
}).catch(e=>showMessage(locMessage,e.message||'Location was not saved.')).finally(()=>save.disabled=false)
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
    if (!services::adsb::waitForIdle(12000)) {
      setUpdateFailure("Aircraft request is still finishing. Please retry the update.");
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
  char response[640];
  snprintf(response, sizeof(response),
           "{\"version\":\"%s\",\"wifi\":\"%s\",\"ip\":\"%s\",\"hostname\":\"%s\","
           "\"lat\":\"%.6f\",\"lon\":\"%.6f\",\"orientation\":\"%s\","
           "\"labelMask\":%u,\"renderedLabelMask\":%u,\"labelCallsign\":%s,\"labelType\":%s,\"labelAltitude\":%s,"
           "\"airportRunways\":%s,\"airportLabels\":%s,\"dimRings\":%s,\"sweep\":%s,\"adsbBusy\":%s,\"adsbUpdates\":%u,\"aircraftCount\":%u,\"freeHeap\":%u,\"adsbStackFree\":%u,\"sweepFrames\":%u,\"sweepMaxGapMs\":%u}",
           firmware::kVersion, connected ? "Connected" : "Disconnected", ip.c_str(),
           wifiPortalHostUrl(),
           services::location::lat(), services::location::lon(),
           ui::radar::topDirectionCode(),
           static_cast<unsigned>(ui::radar::labelMask()),
           static_cast<unsigned>(ui::renderedLabelMask()),
           ui::radar::showCallsign() ? "true" : "false",
           ui::radar::showAircraftType() ? "true" : "false",
           ui::radar::showAltitude() ? "true" : "false",
           ui::radar::showRunways() ? "true" : "false",
           ui::radar::showRunwayLabels() ? "true" : "false",
           ui::radar::dimRings() ? "true" : "false",
           ui::radar::sweepEnabled() ? "true" : "false",
           services::adsb::busy() ? "true" : "false",
           static_cast<unsigned>(services::adsb::completedUpdates()),
           static_cast<unsigned>(services::adsb::aircraftCount()),
           static_cast<unsigned>(ESP.getFreeHeap()),
           static_cast<unsigned>(services::adsb::workerStackFree()),
           static_cast<unsigned>(ui::sweepFrameCount()),
           static_cast<unsigned>(ui::sweepMaxGapMs()));
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", response);
}

void sendOrientationResult(WebServer& server) {
  if (s_update_in_progress) {
    server.send(409, "application/json",
                "{\"ok\":false,\"message\":\"Wait for the firmware update to finish.\"}");
    return;
  }
  if (!server.hasArg("orientation") ||
      !ui::radar::saveTopDirection(server.arg("orientation").c_str())) {
    server.send(400, "application/json",
                "{\"ok\":false,\"message\":\"Choose North, East, South, or West.\"}");
    return;
  }
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json",
              "{\"ok\":true,\"message\":\"Orientation saved.\"}");
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

bool parseBooleanArg(WebServer& server, const char* name, bool* value) {
  if (!server.hasArg(name)) {
    return false;
  }
  const String arg = server.arg(name);
  if (arg == "1") {
    *value = true;
    return true;
  }
  if (arg == "0") {
    *value = false;
    return true;
  }
  return false;
}

char upperAscii(char value) {
  return value >= 'a' && value <= 'z' ? static_cast<char>(value - 'a' + 'A')
                                      : value;
}

bool matchesIcao(const char* query, const char ident[5], bool exact) {
  if (query == nullptr || query[0] == '\0') {
    return false;
  }
  for (size_t i = 0; i < 4; ++i) {
    if (query[i] == '\0') {
      return !exact;
    }
    if (upperAscii(query[i]) != ident[i]) {
      return false;
    }
  }
  return !exact || query[4] == '\0';
}

const data::large_airports::Airport* findAirport(const char* icao) {
  for (size_t i = 0; i < data::large_airports::kAirportCount; ++i) {
    const auto& airport = data::large_airports::kAirports[i];
    if (matchesIcao(icao, airport.ident, true)) {
      return &airport;
    }
  }
  return nullptr;
}

void sendAirportMatches(WebServer& server) {
  const String query_string = server.arg("query");
  char query[5] = {};
  const size_t query_length =
      std::min(static_cast<size_t>(query_string.length()), sizeof(query) - 1);
  for (size_t i = 0; i < query_length; ++i) {
    query[i] = upperAscii(query_string[i]);
  }

  char response[128] = "{\"airports\":[";
  size_t used = strlen(response);
  size_t matches = 0;
  for (size_t i = 0; i < data::large_airports::kAirportCount && matches < 8;
       ++i) {
    const auto& airport = data::large_airports::kAirports[i];
    if (!matchesIcao(query, airport.ident, false)) {
      continue;
    }
    const int written = snprintf(response + used, sizeof(response) - used,
                                 "%s\"%s\"", matches == 0 ? "" : ",",
                                 airport.ident);
    if (written < 0 || static_cast<size_t>(written) >= sizeof(response) - used) {
      break;
    }
    used += static_cast<size_t>(written);
    ++matches;
  }
  snprintf(response + used, sizeof(response) - used, "]}");
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", response);
}

void sendAirportLocationResult(WebServer& server) {
  if (s_update_in_progress) {
    server.send(409, "application/json",
                "{\"ok\":false,\"message\":\"Wait for the firmware update to finish.\"}");
    return;
  }
  if (!server.hasArg("icao")) {
    server.send(400, "application/json",
                "{\"ok\":false,\"message\":\"Enter a four-letter ICAO code.\"}");
    return;
  }
  const data::large_airports::Airport* airport =
      findAirport(server.arg("icao").c_str());
  if (airport == nullptr ||
      !services::location::save(airport->lat_e7 * 1e-7,
                                airport->lon_e7 * 1e-7)) {
    server.send(404, "application/json",
                "{\"ok\":false,\"message\":\"Airport not found in this firmware.\"}");
    return;
  }
  char response[192];
  snprintf(response, sizeof(response),
           "{\"ok\":true,\"message\":\"Base location set to %s.\","
           "\"icao\":\"%s\",\"lat\":\"%.6f\",\"lon\":\"%.6f\"}",
           airport->ident, airport->ident, services::location::lat(),
           services::location::lon());
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", response);
}

void sendSweepResult(WebServer& server) {
  if (s_update_in_progress) {
    server.send(409, "application/json", "{\"ok\":false,\"message\":\"Wait for the firmware update to finish.\"}");
    return;
  }
  bool enabled = false;
  if (!parseBooleanArg(server, "enabled", &enabled)) {
    server.send(400, "application/json", "{\"ok\":false,\"message\":\"Invalid sweep setting.\"}");
    return;
  }
  if (!ui::radar::saveSweepEnabled(enabled)) {
    server.send(500, "application/json", "{\"ok\":false,\"message\":\"Could not save sweep.\"}");
    return;
  }
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", "{\"ok\":true,\"message\":\"Sweep saved.\"}");
}

void sendAppearanceResult(WebServer& server) {
  if (s_update_in_progress) {
    server.send(409, "application/json",
                "{\"ok\":false,\"message\":\"Wait for the firmware update to finish.\"}");
    return;
  }
  bool enabled = false;
  if (!parseBooleanArg(server, "dimRings", &enabled)) {
    server.send(400, "application/json",
                "{\"ok\":false,\"message\":\"Invalid ring dimming setting.\"}");
    return;
  }
  if (!ui::radar::saveDimRings(enabled)) {
    server.send(500, "application/json",
                "{\"ok\":false,\"message\":\"Could not save appearance. Please retry.\"}");
    return;
  }
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json",
              "{\"ok\":true,\"message\":\"Appearance saved.\"}");
}

void sendLabelsResult(WebServer& server) {
  if (s_update_in_progress) {
    server.send(409, "application/json",
                "{\"ok\":false,\"message\":\"Wait for the firmware update to finish.\"}");
    return;
  }
  uint16_t mask = 0;
  bool valid = true;
  if (server.hasArg("mask")) {
    const String arg = server.arg("mask");
    valid = !arg.isEmpty() && arg.length() <= 4;
    for (size_t i = 0; valid && i < arg.length(); ++i) {
      if (arg[i] < '0' || arg[i] > '9') valid = false;
      else mask = mask * 10 + (arg[i] - '0');
    }
  } else {
    // Preserve compatibility with an already-open legacy web page.
    bool callsign = false, type = false, altitude = false;
    valid = parseBooleanArg(server, "callsign", &callsign) &&
            parseBooleanArg(server, "type", &type) &&
            parseBooleanArg(server, "altitude", &altitude);
    mask = (callsign ? 1 : 0) | (type ? 2 : 0) | (altitude ? 4 : 0);
  }
  if (!valid || !services::adsb::validLabelMask(mask)) {
    server.send(400, "application/json",
                "{\"ok\":false,\"message\":\"Choose at most three valid labels.\"}");
    return;
  }
  if (!ui::radar::saveLabelMask(mask)) {
    server.send(500, "application/json",
                "{\"ok\":false,\"message\":\"Could not save labels. Please retry.\"}");
    return;
  }
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json",
              "{\"ok\":true,\"message\":\"Plane labels saved.\"}");
}

void sendAirportsResult(WebServer& server) {
  if (s_update_in_progress) {
    server.send(409, "application/json",
                "{\"ok\":false,\"message\":\"Wait for the firmware update to finish.\"}");
    return;
  }
  bool runways = false;
  bool labels = false;
  if (!parseBooleanArg(server, "runways", &runways) ||
      !parseBooleanArg(server, "labels", &labels)) {
    server.send(400, "application/json",
                "{\"ok\":false,\"message\":\"Invalid airport settings.\"}");
    return;
  }
  ui::radar::saveAirportOverlay(runways, labels);
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json",
              "{\"ok\":true,\"message\":\"Airport settings saved.\"}");
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
    manager->server->on("/api/settings/orientation", HTTP_POST,
                        [manager]() { sendOrientationResult(*manager->server); });
    manager->server->on("/api/settings/sweep", HTTP_POST,
                        [manager]() { sendSweepResult(*manager->server); });
    manager->server->on("/api/settings/appearance", HTTP_POST,
                        [manager]() { sendAppearanceResult(*manager->server); });
    manager->server->on("/api/settings/labels", HTTP_POST,
                        [manager]() { sendLabelsResult(*manager->server); });
    manager->server->on("/api/settings/airports", HTTP_POST,
                        [manager]() { sendAirportsResult(*manager->server); });
    manager->server->on("/api/airports", HTTP_GET,
                        [manager]() { sendAirportMatches(*manager->server); });
    manager->server->on("/api/settings/airport-location", HTTP_POST,
                        [manager]() { sendAirportLocationResult(*manager->server); });
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
