#pragma once

#include <freertos/FreeRTOS.h>

namespace NetworkGate {

bool begin();
void setConnected(bool connected);
bool waitUntilConnected(TickType_t timeout = portMAX_DELAY);
bool isConnected();

}
