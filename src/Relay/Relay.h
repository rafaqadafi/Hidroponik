#pragma once

#include <Arduino.h>

namespace Relay {

bool begin();
bool isReady();
bool setRelay(uint8_t relayNumber, bool on);
bool allOff();
bool allOn();
// Jalur safe-off yang tetap bekerja saat NetworkGate tertutup.
bool failsafeAllOff();
bool releaseFailsafe();
uint8_t getState();

}
