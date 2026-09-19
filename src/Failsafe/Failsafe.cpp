#include "Failsafe.h"

#include <Arduino.h>
#include <esp_task_wdt.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include "Config/config.h"
#include "Relay/Relay.h"

namespace {

constexpr uint8_t MAX_RELAY_CHANNELS = 8;

SemaphoreHandle_t stateMutex = nullptr;
TaskHandle_t taskHandle = nullptr;
uint32_t relayDeadlineMs[MAX_RELAY_CHANNELS + 1] = {};
uint32_t lastHeartbeatMs = 0;
bool heartbeatReceived = false;
bool mqttConnected = false;
bool localControlEnabled = false;
bool localSafetySafe = false;
bool safeStateApplied = false;
bool watchdogAttached = false;

bool readBoolean(const String &json, const char *key, bool &value)
{
    const String marker = String("\"") + key + "\":";
    const int start = json.indexOf(marker);
    if (start < 0) return false;

    const String tail = json.substring(start + marker.length());
    if (tail.startsWith("true")) {
        value = true;
        return true;
    }
    if (tail.startsWith("false")) {
        value = false;
        return true;
    }
    return false;
}

bool readNumber(const String &json, const char *key, float &value)
{
    const String marker = String("\"") + key + "\":";
    const int start = json.indexOf(marker);
    if (start < 0) return false;
    value = json.substring(start + marker.length()).toFloat();
    return true;
}

bool communicationHealthyLocked(uint32_t now)
{
    if (localControlEnabled) return true;
    return mqttConnected && heartbeatReceived &&
           now - lastHeartbeatMs <= Config::Failsafe::HEARTBEAT_TIMEOUT_MS;
}

void clearDeadlinesLocked()
{
    for (uint8_t relay = 0; relay <= MAX_RELAY_CHANNELS; ++relay) {
        relayDeadlineMs[relay] = 0;
    }
}

bool applySafeOff()
{
    const bool applied = Relay::failsafeAllOff();
    if (!applied) {
        Serial.println("FAILSAFE: relay belum siap untuk safe OFF");
    }
    return applied;
}

void requestSafeOff()
{
    if (stateMutex == nullptr) return;

    bool apply = false;
    xSemaphoreTake(stateMutex, portMAX_DELAY);
    clearDeadlinesLocked();
    if (!safeStateApplied) {
        safeStateApplied = true;
        apply = true;
    }
    xSemaphoreGive(stateMutex);

    if (apply && !applySafeOff()) {
        xSemaphoreTake(stateMutex, portMAX_DELAY);
        safeStateApplied = false;
        xSemaphoreGive(stateMutex);
    }
}

void task(void *)
{
    while (true) {
        const uint32_t now = millis();
        uint8_t expiredMask = 0;
        bool applySafeState = false;

        xSemaphoreTake(stateMutex, portMAX_DELAY);
        const bool communicationSafe = communicationHealthyLocked(now);
        const bool systemSafe = communicationSafe && localSafetySafe;

        if (!systemSafe) {
            clearDeadlinesLocked();
            if (!safeStateApplied) {
                safeStateApplied = true;
                applySafeState = true;
            }
        } else {
            safeStateApplied = false;
            for (uint8_t relay = 1; relay <= MAX_RELAY_CHANNELS; ++relay) {
                if (relayDeadlineMs[relay] != 0 &&
                    now - relayDeadlineMs[relay] < 0x80000000UL) {
                    expiredMask |= static_cast<uint8_t>(1U << (relay - 1));
                    relayDeadlineMs[relay] = 0;
                }
            }
        }
        xSemaphoreGive(stateMutex);

        if (applySafeState && !applySafeOff()) {
            xSemaphoreTake(stateMutex, portMAX_DELAY);
            safeStateApplied = false;
            xSemaphoreGive(stateMutex);
        }

        if (expiredMask != 0) {
            // Command duration berakhir. Safe state untuk channel yang habis
            // tetap diproses melalui API Relay, bukan dari callback MQTT.
            for (uint8_t relay = 1; relay <= MAX_RELAY_CHANNELS; ++relay) {
                if (expiredMask & (1U << (relay - 1))) {
                    if (!Relay::setRelay(relay, false)) {
                        applySafeOff();
                        break;
                    }
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(Config::Failsafe::CHECK_INTERVAL_MS));
    }
}

} // namespace

namespace Failsafe {

bool begin()
{
    if (stateMutex != nullptr) return true;

    stateMutex = xSemaphoreCreateMutex();
    if (stateMutex == nullptr) return false;

    clearDeadlinesLocked();
    lastHeartbeatMs = 0;
    heartbeatReceived = false;
    mqttConnected = false;
    localControlEnabled = false;
    localSafetySafe = false;
    safeStateApplied = false;

    const esp_err_t watchdogResult = esp_task_wdt_init(
        Config::Failsafe::WATCHDOG_TIMEOUT_S, true);
    if (watchdogResult != ESP_OK && watchdogResult != ESP_ERR_INVALID_STATE) {
        Serial.printf("FAILSAFE: watchdog init gagal (%d)\n",
                      static_cast<int>(watchdogResult));
    }

    requestSafeOff();
    return xTaskCreate(task, "Failsafe", 4096, nullptr, 2, &taskHandle) == pdPASS;
}

void handleHeartbeat()
{
    if (stateMutex == nullptr) return;
    xSemaphoreTake(stateMutex, portMAX_DELAY);
    lastHeartbeatMs = millis();
    heartbeatReceived = true;
    xSemaphoreGive(stateMutex);
}

void setMqttConnected(bool connected)
{
    bool localMode = false;
    if (stateMutex == nullptr) return;
    xSemaphoreTake(stateMutex, portMAX_DELAY);
    mqttConnected = connected;
    localMode = localControlEnabled;
    xSemaphoreGive(stateMutex);

    if (!connected && !localMode) requestSafeOff();
}

void setLocalControl(bool enabled)
{
    bool safetySafe = false;
    if (stateMutex == nullptr) return;
    xSemaphoreTake(stateMutex, portMAX_DELAY);
    localControlEnabled = enabled;
    safetySafe = localSafetySafe;
    xSemaphoreGive(stateMutex);

    if (!enabled) {
        requestSafeOff();
    } else if (safetySafe) {
        Relay::releaseFailsafe();
    }
}

void updateLocalSafety(bool allFloatsNormal)
{
    bool localMode = false;
    if (stateMutex == nullptr) return;
    xSemaphoreTake(stateMutex, portMAX_DELAY);
    localSafetySafe = allFloatsNormal;
    localMode = localControlEnabled;
    xSemaphoreGive(stateMutex);

    if (!allFloatsNormal) {
        requestSafeOff();
    } else if (localMode) {
        Relay::releaseFailsafe();
    }
}

bool handleControlPayload(const char *payload, size_t length)
{
    if (stateMutex == nullptr || payload == nullptr || length == 0 ||
        length >= Config::Mqtt::BUFFER_SIZE) {
        return false;
    }

    String command;
    command.reserve(length + 1);
    for (size_t index = 0; index < length; ++index) {
        command += payload[index];
    }

    bool emergencyStop = false;
    if (readBoolean(command, "emergency_stop", emergencyStop) &&
        emergencyStop) {
        Serial.println("FAILSAFE: emergency stop diterima");
        requestSafeOff();
        return true;
    }

    float relayValue = -1.0f;
    bool stateOn = false;
    if (!readNumber(command, "relay", relayValue) ||
        !readBoolean(command, "state", stateOn) ||
        relayValue < 0.0f || relayValue > MAX_RELAY_CHANNELS ||
        relayValue != static_cast<float>(static_cast<int>(relayValue))) {
        Serial.println("FAILSAFE: command relay tidak valid");
        return false;
    }

    const uint8_t relayNumber = static_cast<uint8_t>(relayValue);
    uint32_t durationMs = Config::Failsafe::DEFAULT_COMMAND_DURATION_MS;
    float requestedDuration = 0.0f;
    if (readNumber(command, "duration_ms", requestedDuration)) {
        if (requestedDuration <= 0.0f ||
            requestedDuration > Config::Failsafe::MAX_COMMAND_DURATION_MS) {
            Serial.println("FAILSAFE: duration command tidak valid");
            return false;
        }
        durationMs = static_cast<uint32_t>(requestedDuration);
    }

    const uint32_t now = millis();
    xSemaphoreTake(stateMutex, portMAX_DELAY);
    const bool communicationSafe = communicationHealthyLocked(now);
    const bool allowed = !stateOn ||
        (localSafetySafe && communicationSafe);
    xSemaphoreGive(stateMutex);

    if (!allowed) {
        Serial.println("FAILSAFE: command ON ditolak, sistem belum aman");
        requestSafeOff();
        return false;
    }

    bool accepted = false;
    if (stateOn && !Relay::releaseFailsafe()) {
        Serial.println("FAILSAFE: relay belum siap untuk command ON");
        return false;
    }
    if (!stateOn && !communicationSafe) {
        applySafeOff();
        accepted = true;
    } else if (relayNumber == 0) {
        accepted = stateOn ? Relay::allOn() : Relay::allOff();
    } else {
        accepted = Relay::setRelay(relayNumber, stateOn);
    }

    if (!accepted) {
        Serial.println("FAILSAFE: command relay gagal masuk antrean");
        return false;
    }

    xSemaphoreTake(stateMutex, portMAX_DELAY);
    if (stateOn) {
        const uint32_t deadline = now + durationMs;
        if (relayNumber == 0) {
            for (uint8_t relay = 1; relay <= MAX_RELAY_CHANNELS; ++relay) {
                relayDeadlineMs[relay] = deadline;
            }
        } else {
            relayDeadlineMs[relayNumber] = deadline;
        }
    } else if (relayNumber == 0) {
        clearDeadlinesLocked();
    } else {
        relayDeadlineMs[relayNumber] = 0;
    }
    xSemaphoreGive(stateMutex);

    Serial.printf("FAILSAFE: command relay %u %s diterima (%lu ms)\n",
                  relayNumber, stateOn ? "ON" : "OFF",
                  static_cast<unsigned long>(stateOn ? durationMs : 0));
    return true;
}

void attachWatchdog()
{
    if (watchdogAttached) return;
    const esp_err_t result = esp_task_wdt_add(nullptr);
    if (result == ESP_OK || result == ESP_ERR_INVALID_STATE) {
        watchdogAttached = true;
    } else {
        Serial.printf("FAILSAFE: watchdog task gagal dipasang (%d)\n",
                      static_cast<int>(result));
    }
}

void feedWatchdog()
{
    if (watchdogAttached) esp_task_wdt_reset();
}

} // namespace Failsafe
