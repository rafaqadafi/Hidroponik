#include "Relay.h"

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include "Config/config.h"
#include "Network/NetworkGate.h"

namespace {

enum class CommandType : uint8_t {
    SetOne,
    SetAll
};

struct Command {
    CommandType type;
    uint8_t relayNumber;
    bool on;
    uint32_t generation;
};

QueueHandle_t commandQueue = nullptr;
SemaphoreHandle_t stateMutex = nullptr;
uint8_t relayState = 0;
uint32_t commandGeneration = 0;
bool safetyLocked = false;
bool ready = false;

void writeShiftRegister(uint8_t logicalState)
{
    const uint8_t output = Config::Relay::ACTIVE_LOW ? ~logicalState : logicalState;
    digitalWrite(Config::Pins::RELAY_RCLK, LOW);
    shiftOut(Config::Pins::RELAY_SER, Config::Pins::RELAY_SRCLK, MSBFIRST,
             output);
    digitalWrite(Config::Pins::RELAY_RCLK, HIGH);
}

void task(void *)
{
    Command command{};
    while (true) {
        if (!NetworkGate::waitUntilConnected()) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }
        if (xQueueReceive(commandQueue, &command, pdMS_TO_TICKS(1000)) != pdTRUE) {
            continue;
        }
        if (!NetworkGate::waitUntilConnected()) continue;

        xSemaphoreTake(stateMutex, portMAX_DELAY);
        if (command.generation != commandGeneration ||
            (safetyLocked && command.on)) {
            xSemaphoreGive(stateMutex);
            continue;
        }
        if (command.type == CommandType::SetAll) {
            relayState = command.on ? Config::Relay::ALL_ON_MASK : 0x00;
        } else {
            const uint8_t mask = 1U << (command.relayNumber - 1);
            if (command.on) relayState |= mask;
            else relayState &= static_cast<uint8_t>(~mask);
        }
        writeShiftRegister(relayState);
        xSemaphoreGive(stateMutex);

        if (command.type == CommandType::SetAll) {
            Serial.println(command.on ? "Semua relay: ON" : "Semua relay: OFF");
        } else {
            Serial.printf("Relay %u: %s\n", command.relayNumber,
                          command.on ? "ON" : "OFF");
        }
    }
}

bool send(const Command &command)
{
    if (!ready || stateMutex == nullptr) return false;

    xSemaphoreTake(stateMutex, portMAX_DELAY);
    Command queued = command;
    queued.generation = commandGeneration;
    const bool accepted = xQueueSend(commandQueue, &queued, 0) == pdTRUE;
    xSemaphoreGive(stateMutex);
    return accepted;
}

}

namespace Relay {

bool begin()
{
    if (ready) return true;

    pinMode(Config::Pins::RELAY_SER, OUTPUT);
    pinMode(Config::Pins::RELAY_RCLK, OUTPUT);
    pinMode(Config::Pins::RELAY_SRCLK, OUTPUT);
    digitalWrite(Config::Pins::RELAY_SER, LOW);
    digitalWrite(Config::Pins::RELAY_SRCLK, LOW);
    digitalWrite(Config::Pins::RELAY_RCLK, HIGH);

    relayState = 0x00;
    commandGeneration = 0;
    safetyLocked = false;
    writeShiftRegister(relayState);

    commandQueue = xQueueCreate(Config::Relay::COMMAND_QUEUE_LENGTH,
                                 sizeof(Command));
    stateMutex = xSemaphoreCreateMutex();
    if (commandQueue == nullptr || stateMutex == nullptr) return false;

    ready = xTaskCreate(task, "Relay", 3072, nullptr, 2, nullptr) == pdPASS;
    return ready;
}

bool isReady()
{
    return ready;
}

bool setRelay(uint8_t relayNumber, bool on)
{
    if (relayNumber < 1 || relayNumber > Config::Relay::CHANNEL_COUNT) {
        return false;
    }
    return send({CommandType::SetOne, relayNumber, on});
}

bool allOff()
{
    return send({CommandType::SetAll, 0, false});
}

bool allOn()
{
    return send({CommandType::SetAll, 0, true});
}

bool failsafeAllOff()
{
    if (!ready || stateMutex == nullptr) return false;

    xSemaphoreTake(stateMutex, portMAX_DELAY);
    ++commandGeneration;
    safetyLocked = true;
    relayState = 0x00;
    writeShiftRegister(relayState);
    xSemaphoreGive(stateMutex);

    Command discarded{};
    while (xQueueReceive(commandQueue, &discarded, 0) == pdTRUE) {
    }
    return true;
}

bool releaseFailsafe()
{
    if (!ready || stateMutex == nullptr) return false;

    xSemaphoreTake(stateMutex, portMAX_DELAY);
    safetyLocked = false;
    xSemaphoreGive(stateMutex);
    return true;
}

uint8_t getState()
{
    if (stateMutex == nullptr) return 0;
    xSemaphoreTake(stateMutex, portMAX_DELAY);
    const uint8_t value = relayState;
    xSemaphoreGive(stateMutex);
    return value;
}

}
