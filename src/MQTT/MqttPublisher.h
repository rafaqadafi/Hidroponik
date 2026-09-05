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

bool begin();
void publishTds(float ppm, bool valid);
void publishTemperature(float temperatureC, bool valid);
void publishDistance(float distanceCm, bool valid);
void publishLight(float lux, bool valid);
void publishTurbidity(float ntu, bool valid);
void publishPh(float ph, bool valid);
void publishRelayState(uint8_t state);
bool getSystemConfig(SystemConfig &config);
bool isConnected();
bool takeTdsRequest();

}
