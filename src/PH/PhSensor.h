#pragma once

namespace PhSensor {

bool begin();
bool isReady();
float getVoltage();
float getPh(float temperatureC);

}
