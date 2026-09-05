#include "PhSensor.h"

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include "ADS1115/Ads1115Manager.h"
#include "Config/config.h"
#include "Network/NetworkGate.h"

namespace {

constexpr float CALIBRATION_TEMPERATURE_C = 26.62f;

struct CalibrationPoint {
    float voltage;
    float ph;
};

// Tiga titik hasil kalibrasi aktual, dari tegangan rendah ke tinggi.
constexpr CalibrationPoint CAL[] = {
    {1.1648f, 9.1670f},
    {1.5460f, 6.8565f},
    {2.0396f, 4.0100f}
};

SemaphoreHandle_t mutex = nullptr;
float latestVoltage = 0.0f;
bool measurementValid = false;

float interpolate(float voltage, const CalibrationPoint &a,
                  const CalibrationPoint &b)
{
    return a.ph + (voltage - a.voltage) *
           (b.ph - a.ph) / (b.voltage - a.voltage);
}

float voltageToPh(float voltage, float temperatureC)
{
    const float phAtCalibrationTemperature = voltage <= CAL[1].voltage
        ? interpolate(voltage, CAL[0], CAL[1])
        : interpolate(voltage, CAL[1], CAL[2]);

    return 7.0f + (phAtCalibrationTemperature - 7.0f) *
                  (CALIBRATION_TEMPERATURE_C + 273.15f) /
                  (temperatureC + 273.15f);
}

void task(void *)
{
    while (true) {
        if (!NetworkGate::waitUntilConnected()) continue;

        const float voltage =
            Ads1115Manager::readFilteredVoltage(Config::Ph::ADC_CHANNEL);
        const bool valid = voltage > Config::Ph::MIN_VALID_VOLTAGE &&
                           voltage < Config::Ph::MAX_VALID_VOLTAGE;

        xSemaphoreTake(mutex, portMAX_DELAY);
        latestVoltage = voltage;
        measurementValid = valid;
        xSemaphoreGive(mutex);

        vTaskDelay(pdMS_TO_TICKS(Config::Ph::SAMPLE_INTERVAL_MS));
    }
}

}

namespace PhSensor {

bool begin()
{
    if (!Ads1115Manager::isReady()) return false;
    mutex = xSemaphoreCreateMutex();
    return mutex != nullptr &&
           xTaskCreate(task, "pH", 4096, nullptr, 2, nullptr) == pdPASS;
}

bool isReady()
{
    if (mutex == nullptr) return false;
    xSemaphoreTake(mutex, portMAX_DELAY);
    const bool valid = measurementValid;
    xSemaphoreGive(mutex);
    return valid;
}

float getVoltage()
{
    if (mutex == nullptr) return 0.0f;
    xSemaphoreTake(mutex, portMAX_DELAY);
    const float voltage = latestVoltage;
    xSemaphoreGive(mutex);
    return voltage;
}

float getPh(float temperatureC)
{
    return voltageToPh(getVoltage(), temperatureC);
}

}
