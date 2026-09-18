#include "Buzzer.h"
#include <Arduino.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "Config/config.h"

namespace {

bool initialized = false;
volatile bool liquidEmptyRequested = false;

constexpr uint32_t BEEP_ON_MS = 180;
constexpr uint32_t BEEP_OFF_MS = 180;
constexpr uint32_t BEEP_GROUP_PAUSE_MS = 500;

void writeState(bool on)
{
    const bool outputHigh = Config::Buzzer::ACTIVE_HIGH ? on : !on;
    digitalWrite(Config::Pins::BUZZER, outputHigh ? HIGH : LOW);
}

void buzzerTask(void *)
{
    for (;;) {
        if (!liquidEmptyRequested) {
            writeState(false);
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }

        for (uint8_t i = 0; i < 3; ++i) {
            if (!liquidEmptyRequested) break;

            writeState(true);
            vTaskDelay(pdMS_TO_TICKS(BEEP_ON_MS));
            writeState(false);

            if (i < 2) {
                vTaskDelay(pdMS_TO_TICKS(BEEP_OFF_MS));
            }
        }

        if (liquidEmptyRequested) {
            vTaskDelay(pdMS_TO_TICKS(BEEP_GROUP_PAUSE_MS));
        }
    }
}

} // namespace

namespace Buzzer {

bool begin()
{
    pinMode(Config::Pins::BUZZER, OUTPUT);
    writeState(false);
    liquidEmptyRequested = false;
    initialized = true;

    if (xTaskCreate(buzzerTask, "Buzzer", 2048, nullptr, 1, nullptr) != pdPASS) {
        initialized = false;
        writeState(false);
        return false;
    }

    return true;
}

void setLiquidEmpty(bool liquidEmpty)
{
    if (!initialized) return;
    liquidEmptyRequested = liquidEmpty;
}

} // namespace Buzzer
