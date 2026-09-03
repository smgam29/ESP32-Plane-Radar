// Host test: c++ -std=c++17 -Iinclude -I.pio/libdeps/supermini/ArduinoJson/src
//   test/aircraft_labels_test.cpp src/services/aircraft_label_format.cpp -o /tmp/labels-test
#include "services/aircraft_label_format.h"
#include "services/adsb_client.h"
#include <cassert>
#include <cstring>
#include <iostream>

using namespace services::adsb;
int main() {
  const Query query{50.9, -1.0, 25.0f, 7};
  assert(sameQuery(query, query));
  auto changed = query;
  changed.lat += 0.01;
  assert(!sameQuery(query, changed));
  changed = query; changed.lon += 0.01;
  assert(!sameQuery(query, changed));
  changed = query; changed.radius_km = 10;
  assert(!sameQuery(query, changed));
  changed = query; changed.labels = 8;
  assert(!sameQuery(query, changed));
  for (unsigned mask = 0; mask <= 65535; ++mask) {
    const bool expected = mask <= kAllLabelMask && __builtin_popcount(mask) <= 3;
    assert(validLabelMask(mask) == expected);
  }
  JsonDocument doc;
  assert(!deserializeJson(doc, R"({"flight":"BAW123  ","hex":"abcdef",
    "t":"A320","alt_baro":12000,"r":"G-ABCD","gs":123.4,"baro_rate":-640,
    "squawk":"0123","category":"A3","nav_modes":["autopilot","althold","lnav"],
    "dbFlags":1,"emergency":"none"})"));
  const char* expected[] = {"BAW123","A320","12000 ft","G-ABCD","123 kt",
    "-640 ft/m","SQ 0123","CAT A3","AP ALT LNAV","MIL"};
  AircraftLabels out{};
  for (uint8_t i = 0; i < kLabelCount; ++i) {
    formatAircraftLabels(doc.as<JsonObjectConst>(), 1U << i, out);
    assert(out.count == 1);
    assert(out.lines[0].kind == static_cast<Label>(i));
    assert(strcmp(out.lines[0].text, expected[i]) == 0);
    assert(!out.emergency);
  }
  formatAircraftLabels(doc.as<JsonObjectConst>(), 7, out);
  assert(out.count == 3);
  formatAircraftLabels(doc.as<JsonObjectConst>(), 15, out);
  assert(out.count == 0);
  doc["emergency"] = "general";
  formatAircraftLabels(doc.as<JsonObjectConst>(), 0, out);
  assert(out.count == 0 && out.emergency);
  for (const char* state : {"general","lifeguard","minfuel","nordo","unlawful","downed"}) {
    doc["emergency"] = state;
    formatAircraftLabels(doc.as<JsonObjectConst>(), 0, out);
    assert(out.emergency);
  }
  for (const char* state : {"none","reserved","unknown",""}) {
    doc["emergency"] = state;
    formatAircraftLabels(doc.as<JsonObjectConst>(), 0, out);
    assert(!out.emergency);
  }
  for (const char* squawk : {"7500","7600","7700"}) {
    doc["squawk"] = squawk;
    formatAircraftLabels(doc.as<JsonObjectConst>(), 0, out);
    assert(out.emergency);
  }
  doc.clear();
  for (uint8_t i = 0; i < kLabelCount; ++i) {
    formatAircraftLabels(doc.to<JsonObject>(), 1U << i, out);
    assert(out.count == 0 && !out.emergency);
  }
  assert(!deserializeJson(doc, R"({"hex":"abc123","alt_geom":5432,"geom_rate":0,"tas":200,"dbFlags":2})"));
  formatAircraftLabels(doc.as<JsonObjectConst>(), labelBit(Label::GroundSpeed), out);
  assert(out.count == 0);  // Never mislabel TAS as ground speed.
  formatAircraftLabels(doc.as<JsonObjectConst>(), 1 | 4 | 32, out);
  assert(out.count == 3);
  assert(strcmp(out.lines[0].text, "abc123") == 0);
  assert(strcmp(out.lines[1].text, "5432 ft") == 0);
  assert(strcmp(out.lines[2].text, "+0 ft/m") == 0);
  doc["alt_baro"] = "ground";
  formatAircraftLabels(doc.as<JsonObjectConst>(), 4, out);
  assert(strcmp(out.lines[0].text, "GND") == 0);
  assert(!deserializeJson(doc, R"({"flight":"ABCDEFGHIJKLMNOPQRSTUVWXYZ123456789","nav_modes":["autopilot","althold","lnav","vnav","approach","tcas"]})"));
  formatAircraftLabels(doc.as<JsonObjectConst>(), 1 | 256, out);
  assert(out.count == 2);
  assert(strlen(out.lines[0].text) == 23);
  assert(strcmp(out.lines[1].text, "AP ALT LNAV VNAV APP+") == 0);
  std::cout << "All aircraft label tests passed\n";
}
