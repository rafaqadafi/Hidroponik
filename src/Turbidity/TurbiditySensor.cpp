#include "TurbiditySensor.h"

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include "ADS1115/Ads1115Manager.h"
#include "Config/config.h"
#include "Network/NetworkGate.h"

namespace {

SemaphoreHandle_t mutex = nullptr;
float latestVoltage = 0.0f;
bool measurementValid = false;

void task(void *)
{
    while (true) {
        if (!NetworkGate::waitUntilConnected()) continue;

        const float voltage =
            Ads1115Manager::readFilteredVoltage(Config::Turbidity::ADC_CHANNEL);
        const bool valid =
            voltage > Config::Turbidity::MIN_VALID_VOLTAGE &&
            voltage < Config::Turbidity::MAX_VALID_VOLTAGE;

        xSemaphoreTake(mutex, portMAX_DELAY);
        latestVoltage = voltage;
        measurementValid = valid;
        xSemaphoreGive(mutex);

        vTaskDelay(pdMS_TO_TICKS(Config::Turbidity::SAMPLE_INTERVAL_MS));
    }
}

}

namespace TurbiditySensor {

bool begin()
{
    if (!Ads1115Manager::isReady()) return false;
    mutex = xSemaphoreCreateMutex();
    return mutex != nullptr &&
           xTaskCreate(task, "Turbidity", 4096, nullptr, 2, nullptr) == pdPASS;
}

bool isReady()
{
    if (mutex == nullptr) return false;
    xSemaphoreTake(mutex, portMAX_DELAY);
    const bool ready = measurementValid;
    xSemaphoreGive(mutex);
    return ready;
}

bool getVoltage(float &voltage)
{
    if (mutex == nullptr) return false;
    xSemaphoreTake(mutex, portMAX_DELAY);
    voltage = latestVoltage;
    const bool valid = measurementValid;
    xSemaphoreGive(mutex);
    return valid;
}

bool isDirtyWater(float voltage)
{
    return voltage < Config::Turbidity::CLOUDY_WATER_REFERENCE_VOLTAGE;
}

bool isCloudyWater(float voltage)
{
    return voltage >= Config::Turbidity::CLOUDY_WATER_REFERENCE_VOLTAGE &&
           voltage < Config::Turbidity::CLEAR_WATER_THRESHOLD_VOLTAGE;
}

}
