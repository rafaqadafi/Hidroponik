#include "LightSensor.h"

#include <Arduino.h>
#include <Wire.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include "I2C/I2CBus.h"
#include "Config/config.h"
#include "Network/NetworkGate.h"

namespace {

SemaphoreHandle_t valueMutex = nullptr;
float latestLux = 0.0f;
bool measurementValid = false;
bool ready = false;

bool sendCommand(uint8_t command)
{
    Wire.beginTransmission(Config::I2C::BH1750_ADDRESS);
    Wire.write(command);
    return Wire.endTransmission() == 0;
}

bool readLux(float &lux)
{
    if (!I2CBus::take()) return false;
    const uint8_t received =
        Wire.requestFrom(Config::I2C::BH1750_ADDRESS, uint8_t(2));
    if (received != 2) {
        while (Wire.available()) Wire.read();
        I2CBus::give();
        return false;
    }

    const uint16_t raw =
        (static_cast<uint16_t>(Wire.read()) << 8) | Wire.read();
    I2CBus::give();
    lux = raw / Config::Light::LUX_DIVISOR;
    return true;
}

void task(void *)
{
    vTaskDelay(pdMS_TO_TICKS(200));
    while (true) {
        if (!NetworkGate::waitUntilConnected()) continue;

        float lux = 0.0f;
        const bool valid = readLux(lux);

        xSemaphoreTake(valueMutex, portMAX_DELAY);
        if (valid) latestLux = lux;
        measurementValid = valid;
        xSemaphoreGive(valueMutex);

        vTaskDelay(pdMS_TO_TICKS(Config::Light::SAMPLE_INTERVAL_MS));
    }
}

}

namespace LightSensor {

bool begin()
{
    if (ready) return true;
    valueMutex = xSemaphoreCreateMutex();
    if (valueMutex == nullptr || !I2CBus::take()) return false;
    const bool detected = sendCommand(Config::Light::POWER_ON_COMMAND) &&
                          sendCommand(Config::Light::CONTINUOUS_HIGH_RES_MODE);
    I2CBus::give();
    if (!detected) return false;

    ready = xTaskCreate(task, "BH1750", 3072, nullptr, 1, nullptr) == pdPASS;
    return ready;
}

bool isReady()
{
    return ready;
}

bool getLux(float &lux)
{
    if (!ready || valueMutex == nullptr) return false;
    xSemaphoreTake(valueMutex, portMAX_DELAY);
    lux = latestLux;
    const bool valid = measurementValid;
    xSemaphoreGive(valueMutex);
    return valid;
}

}
