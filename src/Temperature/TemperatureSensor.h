#pragma once

#include <Arduino.h>

namespace TemperatureSensor {

bool begin();
bool isReady();
float getCelsius();

}
