#pragma once

#include <Arduino.h>

struct SystemConfig {
    uint16_t day;
    float tdsMin;
    float tdsMax;
    float targetTds;
    float phMin;
    float phMax;
    float targetPh;
    char phase[24];
    char plantingDate[11];
    char systemStatus[20];
    bool received;
};

namespace MqttPublisher {

struct SensorData {
    float tdsPpm;
    bool tdsValid;
    float temperatureC;
    bool temperatureValid;
    float distanceCm;
    bool distanceValid;
    float lightLux;
    bool lightValid;
    float turbidityNtu;
    bool turbidityValid;
    float ph;
    bool phValid;
};

bool begin();
void publishSensors(const SensorData &data);
void publishStatus(uint8_t relayState);
inline void publishRelayState(uint8_t state) { publishStatus(state); }
bool getSystemConfig(SystemConfig &config);
bool isConnected();
bool takeTdsRequest();

}
