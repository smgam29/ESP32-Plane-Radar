#include "services/adsb_client.h"

#include <HTTPClient.h>
#include <WiFiClientSecure.h>

#include <ArduinoJson.h>

#include <cstring>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>

#include "config.h"
#include "services/aircraft_label_format.h"


namespace services::adsb {

namespace {

constexpr char kApiBase[] = "https://opendata.adsb.fi/api/v3/lat/";
constexpr float kKmPerNm = 1.852f;
constexpr int kConnectAttemptMs = 200;
constexpr unsigned long kRequestTimeoutMs = 10000;

Aircraft s_aircraft[kMaxAircraft];
size_t s_aircraft_count = 0;
// Worker owns pending until it publishes Result, then waits for main's ack.
Aircraft s_pending[kMaxAircraft];
struct Result {
  Query query;
  size_t count;
  bool ok;
};
QueueHandle_t s_requests = nullptr;
QueueHandle_t s_results = nullptr;
SemaphoreHandle_t s_ack = nullptr;
TaskHandle_t s_worker = nullptr;
bool s_busy = false;  // Main-loop-owned.
uint32_t s_completed_updates = 0;  // Main-loop-owned.
constexpr size_t kMaxPayloadBytes = 64 * 1024;

int performGetWithPoll(HTTPClient& http) {
  http.setConnectTimeout(kConnectAttemptMs);
  const unsigned long deadline = millis() + kRequestTimeoutMs;
  while (millis() < deadline) {
    const int code = http.GET();
    if (code > 0) {
      return code;
    }
    if (code != HTTPC_ERROR_CONNECTION_REFUSED &&
        code != HTTPC_ERROR_NOT_CONNECTED) {
      return code;
    }
    delay(5);
  }
  return HTTPC_ERROR_READ_TIMEOUT;
}

bool readResponseBodyWithPoll(HTTPClient& http, String& payload) {
  WiFiClient* stream = http.getStreamPtr();
  if (stream == nullptr) {
    return false;
  }

  const int content_length = http.getSize();
  if (content_length > static_cast<int>(kMaxPayloadBytes)) return false;
  if (content_length > 0 &&
      !payload.reserve(static_cast<unsigned>(content_length + 1))) return false;

  uint8_t buffer[512];
  const unsigned long deadline = millis() + kRequestTimeoutMs;
  while (millis() < deadline) {
    const int available = stream->available();
    if (available > 0) {
      const int to_read =
          available > static_cast<int>(sizeof(buffer)) ? static_cast<int>(sizeof(buffer))
                                                       : available;
      const int read_bytes = stream->readBytes(buffer, to_read);
      if (read_bytes > 0) {
        if (payload.length() + read_bytes > kMaxPayloadBytes ||
            !payload.concat(reinterpret_cast<const char*>(buffer),
                            static_cast<unsigned>(read_bytes))) return false;
      }
    }
    if (content_length > 0 &&
        static_cast<int>(payload.length()) >= content_length) {
      break;
    }
    if (!http.connected() && stream->available() <= 0) {
      break;
    }
    delay(1);
  }

  return payload.length() > 0 &&
         (content_length < 0 || static_cast<int>(payload.length()) == content_length);
}

float kmToNauticalMiles(float km) { return km / kKmPerNm; }

bool readJsonFloat(const JsonObject& obj, const char* key, float* out) {
  if (obj[key].is<float>() || obj[key].is<double>() || obj[key].is<int>()) {
    *out = obj[key].as<float>();
    return true;
  }
  return false;
}

float pickNoseHeading(const JsonObject& plane) {
  float v = 0.0f;
  if (readJsonFloat(plane, "true_heading", &v)) {
    return v;
  }
  if (readJsonFloat(plane, "mag_heading", &v)) {
    return v;
  }
  if (readJsonFloat(plane, "track", &v)) {
    return v;
  }
  if (readJsonFloat(plane, "dir", &v)) {
    return v;
  }
  return 0.0f;
}

float pickTrackHeading(const JsonObject& plane) {
  float v = 0.0f;
  if (readJsonFloat(plane, "track", &v)) {
    return v;
  }
  if (readJsonFloat(plane, "true_heading", &v)) {
    return v;
  }
  if (readJsonFloat(plane, "mag_heading", &v)) {
    return v;
  }
  if (readJsonFloat(plane, "dir", &v)) {
    return v;
  }
  return 0.0f;
}

float pickGroundSpeed(const JsonObject& plane) {
  float v = 0.0f;
  if (readJsonFloat(plane, "gs", &v)) {
    return v;
  }
  if (readJsonFloat(plane, "tas", &v)) {
    return v;
  }
  if (readJsonFloat(plane, "ias", &v)) {
    return v;
  }
  return 0.0f;
}

bool isOnGround(const JsonObject& plane) {
  if (!plane["alt_baro"].is<const char*>()) {
    return false;
  }
  return strcmp(plane["alt_baro"].as<const char*>(), "ground") == 0;
}

bool fetchUpdate(const Query& query, size_t& count) {
  const float dist_nm = kmToNauticalMiles(query.radius_km);

  String url = kApiBase;
  url += String(query.lat, 6);
  url += "/lon/";
  url += String(query.lon, 6);
  url += "/dist/";
  url += String(dist_nm, 1);

  WiFiClientSecure client;
  client.setInsecure();
  client.setHandshakeTimeout(8);

  HTTPClient http;
  if (!http.begin(client, url)) {
    Serial.println("adsb: http.begin failed");
    return false;
  }

  http.useHTTP10(true);
  http.setTimeout(kRequestTimeoutMs);
  const int code = performGetWithPoll(http);
  if (code != HTTP_CODE_OK) {
    Serial.printf("adsb: HTTP %d\n", code);
    http.end();
    return false;
  }

  String payload;
  if (!readResponseBodyWithPoll(http, payload)) {
    Serial.println("adsb: empty response");
    http.end();
    return false;
  }
  http.end();

  JsonDocument filter;
  const char* fields[] = {"lat", "lon", "true_heading", "mag_heading", "track", "dir",
      "gs", "tas", "ias", "flight", "hex", "t", "alt_baro", "alt_geom", "r",
      "baro_rate", "geom_rate", "squawk", "category", "nav_modes", "dbFlags", "emergency"};
  for (const char* field : fields) filter["ac"][0][field] = true;
  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, payload,
      DeserializationOption::Filter(filter));
  if (err) {
    Serial.printf("adsb: JSON parse error: %s\n", err.c_str());
    return false;
  }

  JsonArray ac = doc["ac"].as<JsonArray>();
  if (ac.isNull()) {
    return false;  // A malformed response must not clear working aircraft.
  }

  size_t n = 0;
  for (JsonObject plane : ac) {
    if (n >= kMaxAircraft) {
      break;
    }
    if (!plane["lat"].is<float>() || !plane["lon"].is<float>()) {
      continue;
    }
    if (isOnGround(plane) && !config::kAdsbShowGroundAircraft) {
      continue;
    }

    s_pending[n].lat = plane["lat"].as<float>();
    s_pending[n].lon = plane["lon"].as<float>();
    s_pending[n].nose_deg = pickNoseHeading(plane);
    s_pending[n].track_deg = pickTrackHeading(plane);
    s_pending[n].gs_knots = pickGroundSpeed(plane);
    formatAircraftLabels(plane, query.labels, s_pending[n].labels);
    ++n;
  }

  count = n;
  Serial.printf("adsb: %u aircraft\n", static_cast<unsigned>(n));
  return true;
}

void fetchTask(void*) {
  Query query{};
  for (;;) {
    if (xQueueReceive(s_requests, &query, portMAX_DELAY) != pdTRUE) continue;
    Result result{query, 0, false};
    result.ok = fetchUpdate(query, result.count);
    // HTTP/TLS/JSON objects are now destroyed before publishing or waiting.
    xQueueSend(s_results, &result, portMAX_DELAY);
    xSemaphoreTake(s_ack, portMAX_DELAY);
  }
}

bool receiveResult(Result& result, TickType_t wait) {
  return s_busy && xQueueReceive(s_results, &result, wait) == pdTRUE;
}
void acknowledge() {
  s_busy = false;
  xSemaphoreGive(s_ack);
}
}  // namespace

size_t aircraftCount() { return s_aircraft_count; }
const Aircraft* aircraftList() { return s_aircraft; }
bool busy() { return s_busy; }
uint32_t completedUpdates() { return s_completed_updates; }
uint32_t workerStackFree() { return s_worker ? uxTaskGetStackHighWaterMark(s_worker) : 0; }

bool begin() {
  if (s_worker) return true;
  s_requests = xQueueCreate(1, sizeof(Query));
  s_results = xQueueCreate(1, sizeof(Result));
  s_ack = xSemaphoreCreateBinary();
  // ESP-IDF uses bytes for task stack size. Equal priority to Arduino loop;
  // scheduler time slicing lets rendering continue through blocking HTTPS.
  if (s_requests && s_results && s_ack &&
      xTaskCreate(fetchTask, "adsb-fetch", 8192, nullptr, 1, &s_worker) == pdPASS)
    return true;
  if (s_requests) vQueueDelete(s_requests);
  if (s_results) vQueueDelete(s_results);
  if (s_ack) vSemaphoreDelete(s_ack);
  s_requests = nullptr;
  s_results = nullptr;
  s_ack = nullptr;
  s_worker = nullptr;
  Serial.println("adsb: worker allocation failed");
  return false;
}

bool requestUpdate(const Query& query) {
  if (s_busy || !s_worker) return false;
  if (xQueueSend(s_requests, &query, 0) != pdTRUE) return false;
  s_busy = true;
  return true;
}

bool applyUpdate(const Query& current) {
  Result result{};
  if (!receiveResult(result, 0)) return false;
  const bool apply = result.ok && sameQuery(result.query, current);
  if (apply) {
    memcpy(s_aircraft, s_pending, result.count * sizeof(Aircraft));
    s_aircraft_count = result.count;
    ++s_completed_updates;
  }
  acknowledge();  // Only now may the worker reuse s_pending.
  return apply;
}

bool waitForIdle(uint32_t timeout_ms) {
  if (!s_busy) return true;
  Result result{};
  if (!receiveResult(result, pdMS_TO_TICKS(timeout_ms))) return false;
  acknowledge();  // Discard during OTA, without changing the displayed snapshot.
  return true;
}
}  // namespace services::adsb
