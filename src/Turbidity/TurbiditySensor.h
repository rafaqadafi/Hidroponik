#pragma once

namespace TurbiditySensor {

bool begin();
bool isReady();
bool getReading(float &ntu, float &voltage);

}
