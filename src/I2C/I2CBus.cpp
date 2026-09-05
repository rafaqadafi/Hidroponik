#include "I2CBus.h"

#include <freertos/semphr.h>

namespace {
SemaphoreHandle_t mutex = nullptr;
}

namespace I2CBus {

bool begin()
{
    if (mutex != nullptr) return true;
    mutex = xSemaphoreCreateMutex();
    return mutex != nullptr;
}

bool take(TickType_t timeout)
{
    return mutex != nullptr && xSemaphoreTake(mutex, timeout) == pdTRUE;
}

void give()
{
    if (mutex != nullptr) xSemaphoreGive(mutex);
}

}
