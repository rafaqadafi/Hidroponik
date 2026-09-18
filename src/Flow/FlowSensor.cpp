#include "FlowSensor.h"

#include <Arduino.h>
#include "Config/config.h"
#include "freertos/FreeRTOS.h"

namespace {

volatile uint32_t pulseCount = 0;
portMUX_TYPE pulseMux = portMUX_INITIALIZER_UNLOCKED;
bool initialized = false;
uint32_t lastSampleMs = 0;
uint32_t lastSamplePulses = 0;
float latestFlowRateLpm = 0.0f;
float latestVolumeLiters = 0.0f;

void IRAM_ATTR onPulse()
{
    portENTER_CRITICAL_ISR(&pulseMux);
    ++pulseCount;
    portEXIT_CRITICAL_ISR(&pulseMux);
}

uint32_t readPulseCount()
{
    portENTER_CRITICAL(&pulseMux);
    const uint32_t count = pulseCount;
    portEXIT_CRITICAL(&pulseMux);
    return count;
}

} // namespace

namespace FlowSensor {

bool begin()
{
    pinMode(Config::Pins::FLOW_SENSOR, INPUT);
    pulseCount = 0;
    lastSamplePulses = 0;
    lastSampleMs = millis();
    latestFlowRateLpm = 0.0f;
    latestVolumeLiters = 0.0f;

    attachInterrupt(digitalPinToInterrupt(Config::Pins::FLOW_SENSOR),
                    onPulse, RISING);
    initialized = true;
    return true;
}

bool getReading(float &flowRateLpm, float &volumeLiters)
{
    if (!initialized) return false;

    const uint32_t now = millis();
    const uint32_t elapsedMs = now - lastSampleMs;
    if (elapsedMs >= Config::Flow::SAMPLE_INTERVAL_MS) {
        const uint32_t currentPulses = readPulseCount();
        const uint32_t pulseDelta = currentPulses - lastSamplePulses;

        latestFlowRateLpm =
            (static_cast<float>(pulseDelta) /
             Config::Flow::PULSES_PER_LITER) *
            (60000.0f / static_cast<float>(elapsedMs));
        latestVolumeLiters =
            static_cast<float>(currentPulses) /
            Config::Flow::PULSES_PER_LITER;

        lastSamplePulses = currentPulses;
        lastSampleMs = now;
    }

    flowRateLpm = latestFlowRateLpm;
    volumeLiters = latestVolumeLiters;
    return true;
}

} // namespace FlowSensor
