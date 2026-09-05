#pragma once

#include <freertos/FreeRTOS.h>

namespace I2CBus {

bool begin();
bool take(TickType_t timeout = portMAX_DELAY);
void give();

}
