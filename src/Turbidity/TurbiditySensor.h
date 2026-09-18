#pragma once

namespace TurbiditySensor {

bool begin();
bool isReady();
bool getVoltage(float &voltage);
bool isDirtyWater(float voltage);

}
