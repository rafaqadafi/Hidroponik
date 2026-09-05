#pragma once

#include <Arduino.h>

namespace Relay {

bool begin();
bool isReady();
bool setRelay(uint8_t relayNumber, bool on);
bool allOff();
bool allOn();
uint8_t getState();

}
