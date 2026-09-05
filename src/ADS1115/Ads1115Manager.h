#pragma once

#include <Arduino.h>

namespace Ads1115Manager {

bool begin();
bool isReady();
float readFilteredVoltage(uint8_t channel);

}
