#pragma once

namespace LightSensor {

bool begin();
bool isReady();
bool getLux(float &lux);

}
