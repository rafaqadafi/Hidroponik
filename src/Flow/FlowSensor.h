#pragma once

#include <stdint.h>

namespace FlowSensor {

bool begin();
bool getReading(float &flowRateLpm, float &volumeLiters);

} // namespace FlowSensor
