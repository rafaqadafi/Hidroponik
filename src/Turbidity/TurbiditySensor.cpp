#include "TurbiditySensor.h"

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include "ADS1115/Ads1115Manager.h"
#include "Config/config.h"
#include "Network/NetworkGate.h"

namespace {

struct CalibrationPoint {
    float voltage;
    float ntu;
};

// Hasil kalibrasi dengan voltage divider yang sama seperti rangkaian utama.
constexpr CalibrationPoint CAL[] = {
    {2.81200f,   0.43f},
    {2.41055f,  18.20f},
    // Median gabungan dua pengulangan kopi dengan prosedur waktu tetap.
    {1.51295f, 186.00f}
};

SemaphoreHandle_t mutex = nullptr;
float latestVoltage = 0.0f;
float latestNtu = 0.0f;
bool measurementValid = false;

float interpolate(float voltage, const CalibrationPoint &a,
                  const CalibrationPoint &b)
{
    return a.ntu + (voltage - a.voltage) *
           (b.ntu - a.ntu) / (b.voltage - a.voltage);
}

float voltageToNtu(float voltage)
{
    float ntu;
    if (voltage >= CAL[1].voltage) {
        ntu = interpolate(voltage, CAL[0], CAL[1]);
    } else {
        ntu = interpolate(voltage, CAL[1], CAL[2]);
    }
    return max(0.0f, ntu);
}

void task(void *)
{
    while (true) {
        if (!NetworkGate::waitUntilConnected()) continue;

        const float voltage =
            Ads1115Manager::readFilteredVoltage(Config::Turbidity::ADC_CHANNEL);
        const bool valid =
            voltage > Config::Turbidity::MIN_VALID_VOLTAGE &&
            voltage < Config::Turbidity::MAX_VALID_VOLTAGE;
        const float ntu = valid ? voltageToNtu(voltage) : 0.0f;

        xSemaphoreTake(mutex, portMAX_DELAY);
        latestVoltage = voltage;
        latestNtu = ntu;
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

bool getReading(float &ntu, float &voltage)
{
    if (mutex == nullptr) return false;
    xSemaphoreTake(mutex, portMAX_DELAY);
    ntu = latestNtu;
    voltage = latestVoltage;
    const bool valid = measurementValid;
    xSemaphoreGive(mutex);
    return valid;
}

}
