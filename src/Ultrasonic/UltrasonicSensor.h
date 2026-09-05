#pragma once

#include <stdint.h>

namespace UltrasonicSensor {

bool begin();
bool isReady();
bool getDistanceCm(float &distanceCm);
uint32_t getLastEchoDurationUs();

}
