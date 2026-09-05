#include "UltrasonicSensor.h"

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include "Config/config.h"
#include "Network/NetworkGate.h"

namespace {

constexpr float SOUND_SPEED_CM_PER_US = 0.0343f;
constexpr float MIN_DISTANCE_CM = 20.0f;
constexpr float MAX_DISTANCE_CM = 600.0f;
// Regresi linear dari tiga titik aktual/terbaca terbaru:
// 30/30.5 cm, 60/60.1 cm, dan 100/100.5 cm.
constexpr float DISTANCE_CALIBRATION_SCALE = 1.0337083f;
constexpr float DISTANCE_CALIBRATION_OFFSET_CM = 0.9926219f;

SemaphoreHandle_t mutex = nullptr;
float latestDistanceCm = 0.0f;
bool measurementAvailable = false;
bool measurementValid = false;
uint32_t latestEchoDurationUs = 0;
TaskHandle_t ultrasonicTaskHandle = nullptr;
volatile uint32_t echoRiseUs = 0;
volatile uint32_t capturedEchoUs = 0;
volatile bool waitingForEcho = false;
constexpr uint8_t MAX_MEDIAN_WINDOW = 15;

uint8_t medianWindow()
{
    if (Config::Ultrasonic::MEDIAN_WINDOW == 0) return 1;
    return Config::Ultrasonic::MEDIAN_WINDOW > MAX_MEDIAN_WINDOW
        ? MAX_MEDIAN_WINDOW
        : Config::Ultrasonic::MEDIAN_WINDOW;
}

void IRAM_ATTR echoInterrupt()
{
    if (!waitingForEcho) return;

    if (digitalRead(Config::Pins::ULTRASONIC_ECHO) == HIGH) {
        echoRiseUs = micros();
        return;
    }

    if (echoRiseUs == 0) return;
    capturedEchoUs = micros() - echoRiseUs;
    waitingForEcho = false;

    BaseType_t taskWoken = pdFALSE;
    if (ultrasonicTaskHandle != nullptr) {
        vTaskNotifyGiveFromISR(ultrasonicTaskHandle, &taskWoken);
    }
    if (taskWoken == pdTRUE) portYIELD_FROM_ISR();
}

float median(const float *samples, uint8_t count)
{
    float sorted[MAX_MEDIAN_WINDOW];
    for (uint8_t i = 0; i < count; i++) sorted[i] = samples[i];

    for (uint8_t i = 1; i < count; i++) {
        const float current = sorted[i];
        int8_t j = i - 1;
        while (j >= 0 && sorted[j] > current) {
            sorted[j + 1] = sorted[j];
            j--;
        }
        sorted[j + 1] = current;
    }
    return sorted[count / 2];
}

bool readOnce(float &distanceCm, uint32_t &durationUs)
{
    ulTaskNotifyTake(pdTRUE, 0);
    echoRiseUs = 0;
    capturedEchoUs = 0;
    waitingForEcho = true;

    digitalWrite(Config::Pins::ULTRASONIC_TRIGGER, LOW);
    delayMicroseconds(Config::Ultrasonic::TRIGGER_SETTLE_US);
    digitalWrite(Config::Pins::ULTRASONIC_TRIGGER, HIGH);
    delayMicroseconds(Config::Ultrasonic::TRIGGER_PULSE_US);
    digitalWrite(Config::Pins::ULTRASONIC_TRIGGER, LOW);

    if (ulTaskNotifyTake(
            pdTRUE,
            pdMS_TO_TICKS(Config::Ultrasonic::ECHO_WAIT_TIMEOUT_MS)) == 0) {
        waitingForEcho = false;
        durationUs = 0;
        return false;
    }

    durationUs = capturedEchoUs;
    if (durationUs == 0 ||
        durationUs > Config::Ultrasonic::ECHO_TIMEOUT_US) return false;

    distanceCm = durationUs * SOUND_SPEED_CM_PER_US * 0.5f *
                 DISTANCE_CALIBRATION_SCALE +
                 DISTANCE_CALIBRATION_OFFSET_CM;
    return distanceCm >= MIN_DISTANCE_CM && distanceCm <= MAX_DISTANCE_CM;
}

void task(void *)
{
    ultrasonicTaskHandle = xTaskGetCurrentTaskHandle();
    const uint8_t windowSize = medianWindow();
    float validSamples[MAX_MEDIAN_WINDOW];
    uint8_t sampleCount = 0;
    uint8_t writePosition = 0;
    TickType_t lastValidTick = 0;
    bool everValid = false;

    while (true) {
        if (!NetworkGate::waitUntilConnected()) continue;

        float distance = 0.0f;
        uint32_t durationUs = 0;
        const bool validNow = readOnce(distance, durationUs);
        const TickType_t now = xTaskGetTickCount();

        if (validNow) {
            validSamples[writePosition] = distance;
            writePosition = (writePosition + 1) % windowSize;
            if (sampleCount < windowSize) sampleCount++;
            lastValidTick = now;
            everValid = true;
        }

        xSemaphoreTake(mutex, portMAX_DELAY);
        measurementAvailable = true;
        latestEchoDurationUs = durationUs;
        if (validNow) latestDistanceCm = median(validSamples, sampleCount);
        measurementValid = everValid &&
            now - lastValidTick <=
                pdMS_TO_TICKS(Config::Ultrasonic::STALE_TIMEOUT_MS);
        xSemaphoreGive(mutex);

        vTaskDelay(pdMS_TO_TICKS(Config::Ultrasonic::SAMPLE_INTERVAL_MS));
    }
}

}

namespace UltrasonicSensor {

bool begin()
{
    pinMode(Config::Pins::ULTRASONIC_TRIGGER, OUTPUT);
    pinMode(Config::Pins::ULTRASONIC_ECHO, INPUT);
    digitalWrite(Config::Pins::ULTRASONIC_TRIGGER, LOW);
    attachInterrupt(digitalPinToInterrupt(Config::Pins::ULTRASONIC_ECHO),
                    echoInterrupt, CHANGE);

    mutex = xSemaphoreCreateMutex();
    return mutex != nullptr &&
           xTaskCreate(task, "Ultrasonic", 3072, nullptr, 2, nullptr) == pdPASS;
}

bool isReady()
{
    return mutex != nullptr;
}

bool getDistanceCm(float &distanceCm)
{
    if (mutex == nullptr) return false;
    xSemaphoreTake(mutex, portMAX_DELAY);
    const bool valid = measurementAvailable && measurementValid;
    distanceCm = latestDistanceCm;
    xSemaphoreGive(mutex);
    return valid;
}

uint32_t getLastEchoDurationUs()
{
    if (mutex == nullptr) return 0;
    xSemaphoreTake(mutex, portMAX_DELAY);
    const uint32_t value = latestEchoDurationUs;
    xSemaphoreGive(mutex);
    return value;
}

}
