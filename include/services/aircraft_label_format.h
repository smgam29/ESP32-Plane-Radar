#pragma once
#include <ArduinoJson.h>
#include "services/aircraft_labels.h"

namespace services::adsb {
void readAircraftLabelData(JsonObjectConst plane, AircraftLabelData& out);
void formatAircraftLabels(JsonObjectConst plane, uint16_t mask,
                          AircraftLabels& out);
}
