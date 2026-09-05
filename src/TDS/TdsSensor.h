#pragma once

#include <Arduino.h>

namespace TdsSensor {

bool begin();
bool isReady();
float getVoltage();
float getPPM(float temperatureC);

}
