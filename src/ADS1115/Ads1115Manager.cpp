#include "Ads1115Manager.h"

#include <Wire.h>
#include <Adafruit_ADS1X15.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include "I2C/I2CBus.h"
#include "Config/config.h"

namespace {

Adafruit_ADS1115 ads;
SemaphoreHandle_t adcMutex = nullptr;
bool ready = false;
constexpr uint8_t MAX_MEDIAN_SAMPLE_COUNT = 15;

uint8_t medianSampleCount()
{
    if (Config::Adc::MEDIAN_SAMPLE_COUNT == 0) return 1;
    return Config::Adc::MEDIAN_SAMPLE_COUNT > MAX_MEDIAN_SAMPLE_COUNT
        ? MAX_MEDIAN_SAMPLE_COUNT
        : Config::Adc::MEDIAN_SAMPLE_COUNT;
}

int16_t medianRaw(uint8_t channel)
{
    const uint8_t sampleCount = medianSampleCount();
    int16_t values[MAX_MEDIAN_SAMPLE_COUNT];
    for (uint8_t i = 0; i < sampleCount; i++) {
        values[i] = ads.readADC_SingleEnded(channel);
        vTaskDelay(pdMS_TO_TICKS(Config::Adc::SAMPLE_DELAY_MS));
    }

    for (uint8_t i = 1; i < sampleCount; i++) {
        const int16_t current = values[i];
        int8_t j = i - 1;
        while (j >= 0 && values[j] > current) {
            values[j + 1] = values[j];
            j--;
        }
        values[j + 1] = current;
    }
    return values[sampleCount / 2];
}

}

namespace Ads1115Manager {

bool begin()
{
    if (ready) return true;
    if (!I2CBus::take()) return false;
    const bool detected = ads.begin(Config::I2C::ADS1115_ADDRESS, &Wire);
    I2CBus::give();
    if (!detected) return false;

    ads.setGain(GAIN_ONE);
    adcMutex = xSemaphoreCreateMutex();
    ready = adcMutex != nullptr;
    return ready;
}

bool isReady()
{
    return ready;
}

float readFilteredVoltage(uint8_t channel)
{
    if (!ready || channel > 3) return 0.0f;

    xSemaphoreTake(adcMutex, portMAX_DELAY);
    I2CBus::take();
    int32_t total = 0;
    const uint8_t averageCount = Config::Adc::AVERAGE_COUNT == 0
        ? 1
        : Config::Adc::AVERAGE_COUNT;
    for (uint8_t i = 0; i < averageCount; i++) {
        total += medianRaw(channel);
    }
    I2CBus::give();
    xSemaphoreGive(adcMutex);

    return max(0.0f, total / static_cast<float>(averageCount)) *
           Config::Adc::LSB_VOLTS_GAIN_ONE;
}

}
