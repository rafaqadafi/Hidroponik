#include "TdsSensor.h"
#include "../ADS1115/Ads1115Manager.h"

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include "Config/config.h"
#include "Network/NetworkGate.h"

namespace {

// Berdasarkan 359 ppm @25 C dan 373 ppm @27.38 C.
constexpr float ALPHA = 0.0163854f;
constexpr float ZERO_VOLTAGE = 0.00575f;
constexpr float DRY_THRESHOLD = 0.0065f;

struct Point {
    float compensatedVoltage;
    float ppm25;
};

// Tegangan setiap buffer dinormalisasi ke 25 C.
constexpr Point CAL[] = {
    {0.9720912f,  359.0f},
    {1.3183017f,  500.0f},
    {1.7088979f,  718.0f},
    {2.2741044f, 1000.0f}
};

SemaphoreHandle_t mutex = nullptr;
float latestVoltage = 0.0f;
bool ready = false;

float interpolate(float voltage, const Point &a, const Point &b)
{
    return a.ppm25 + (voltage - a.compensatedVoltage) *
           (b.ppm25 - a.ppm25) /
           (b.compensatedVoltage - a.compensatedVoltage);
}

float voltageToPPM(float voltage)
{
    if (voltage <= DRY_THRESHOLD) return 0.0f;
    if (voltage <= CAL[0].compensatedVoltage) {
        return (voltage - ZERO_VOLTAGE) * CAL[0].ppm25 /
               (CAL[0].compensatedVoltage - ZERO_VOLTAGE);
    }
    if (voltage <= CAL[1].compensatedVoltage) return interpolate(voltage, CAL[0], CAL[1]);
    if (voltage <= CAL[2].compensatedVoltage) return interpolate(voltage, CAL[1], CAL[2]);
    return interpolate(voltage, CAL[2], CAL[3]);
}

void task(void *)
{
    while (true) {
        if (!NetworkGate::waitUntilConnected()) continue;

        const float voltage = Ads1115Manager::readFilteredVoltage(
            Config::Tds::ADC_CHANNEL);
        xSemaphoreTake(mutex, portMAX_DELAY);
        latestVoltage = voltage;
        ready = true;
        xSemaphoreGive(mutex);
        vTaskDelay(pdMS_TO_TICKS(Config::Tds::SAMPLE_INTERVAL_MS));
    }
}

}

namespace TdsSensor {

bool begin()
{
    if (!Ads1115Manager::isReady()) return false;
    mutex = xSemaphoreCreateMutex();
    return mutex != nullptr &&
           xTaskCreate(task, "TDS", 4096, nullptr, 2, nullptr) == pdPASS;
}

bool isReady()
{
    if (mutex == nullptr) return false;
    xSemaphoreTake(mutex, portMAX_DELAY);
    const bool value = ready;
    xSemaphoreGive(mutex);
    return value;
}

float getVoltage()
{
    if (mutex == nullptr) return 0.0f;
    xSemaphoreTake(mutex, portMAX_DELAY);
    const float value = latestVoltage;
    xSemaphoreGive(mutex);
    return value;
}

float getPPM(float temperatureC)
{
    const float factor = 1.0f + ALPHA * (temperatureC - 25.0f);
    const float compensatedVoltage = getVoltage() / factor;
    const float ppm25 = voltageToPPM(compensatedVoltage);
    return max(0.0f, ppm25 * factor);
}

}
