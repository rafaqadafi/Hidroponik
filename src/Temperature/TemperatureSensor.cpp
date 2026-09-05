#include "TemperatureSensor.h"

#include <OneWire.h>
#include <DallasTemperature.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include "Config/config.h"
#include "Network/NetworkGate.h"

namespace {

OneWire oneWire(Config::Pins::DS18B20);
DallasTemperature sensor(&oneWire);
SemaphoreHandle_t mutex = nullptr;
float latestTemperature = DEVICE_DISCONNECTED_C;
bool ready = false;

void task(void *)
{
    while (true) {
        if (!NetworkGate::waitUntilConnected()) continue;

        sensor.requestTemperatures();
        const float value = sensor.getTempCByIndex(0);
        xSemaphoreTake(mutex, portMAX_DELAY);
        latestTemperature = value;
        ready = true;
        xSemaphoreGive(mutex);
        vTaskDelay(pdMS_TO_TICKS(Config::Temperature::SAMPLE_INTERVAL_MS));
    }
}

}

namespace TemperatureSensor {

bool begin()
{
    sensor.begin();
    sensor.setResolution(12);
    mutex = xSemaphoreCreateMutex();
    return mutex != nullptr &&
           xTaskCreate(task, "Temperature", 4096, nullptr, 2, nullptr) == pdPASS;
}

bool isReady()
{
    if (mutex == nullptr) return false;
    xSemaphoreTake(mutex, portMAX_DELAY);
    const bool value = ready;
    xSemaphoreGive(mutex);
    return value;
}

float getCelsius()
{
    if (mutex == nullptr) return DEVICE_DISCONNECTED_C;
    xSemaphoreTake(mutex, portMAX_DELAY);
    const float value = latestTemperature;
    xSemaphoreGive(mutex);
    return value;
}

}
