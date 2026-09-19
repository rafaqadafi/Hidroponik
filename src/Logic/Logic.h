#pragma once

#include <stdint.h>

namespace Logic {

enum class State : uint8_t {
    Init,
    Monitoring,
    Filling,
    Conditioning,
    Release,
    Fault
};

struct Inputs {
    float temperatureC;
    bool temperatureValid;
    float ph;
    bool phValid;
    float tdsPpm;
    bool tdsValid;
    float turbidityVoltage;
    bool turbidityValid;
    bool turbidityCloudy;
    bool turbidityDirty;
    float lightLux;
    bool lightValid;
    float distanceCm;
    bool distanceValid;
    float flowRateLpm;
    bool flowValid;
    bool phUpNormal;
    bool nutrientANormal;
    bool nutrientBNormal;
    bool phDownNormal;
    bool resetFault;
};

struct ActuatorState {
    bool phUpPump;
    bool phDownPump;
    bool nutrientPump;
    bool waterPump;
    bool solenoidValve;
    bool growLight;
    bool aerator;
    bool buzzerTemperature;
    bool buzzerFault;
    State state;
    uint16_t plantingDay;
    uint8_t plantingWeek;
    float targetTds;
    char faultReason[32];
};

bool begin();
ActuatorState evaluate(const Inputs &inputs);
bool applyActuators(const ActuatorState &state);
uint8_t actuatorMask(const ActuatorState &state);
const char *stateName(State state);

} // namespace Logic
