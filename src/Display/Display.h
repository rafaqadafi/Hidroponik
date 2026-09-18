#pragma once

#include <stdint.h>

namespace Display {

struct SensorViewData {
    float temperature;
    bool tempValid;
    float ph;
    bool phValid;
    float tds;
    bool tdsValid;
    float distance;
    bool distanceValid;
    bool phUpNormal;
    bool phUpValid;
    bool nutrientANormal;
    bool nutrientAValid;
    bool nutrientBNormal;
    bool nutrientBValid;
    bool phDownNormal;
    bool phDownValid;
    float light;
    bool lightValid;
    float turbidityVoltage;
    bool turbidityValid;
    bool turbidityDirty;
    float flowRateLpm;
    float flowVolumeLiters;
    bool flowValid;
    bool wifiConnected;
    bool mqttConnected;
};

bool begin();
void update(const SensorViewData &data);

} // namespace Display
