#include "MqttPublisher.h"

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <PubSubClient.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include "Failsafe/Failsafe.h"
#include "Config/config.h"
#include "Network/NetworkGate.h"

namespace {

struct Message {
    enum class Topic : uint8_t {
        Sensor,
        Status
    } topic;
    char payload[1024];
};

WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);
WiFiManager wifiManager;
QueueHandle_t queue = nullptr;
SemaphoreHandle_t configMutex = nullptr;
SystemConfig systemConfig{};
portMUX_TYPE requestMux = portMUX_INITIALIZER_UNLOCKED;
bool tdsRequested = false;
portMUX_TYPE mqttStateMux = portMUX_INITIALIZER_UNLOCKED;
bool mqttConnected = false;
bool systemConfigReceived = false;
bool tdsConfigReceived = false;
bool phConfigReceived = false;

void setMqttConnected(bool connected)
{
    portENTER_CRITICAL(&mqttStateMux);
    mqttConnected = connected;
    portEXIT_CRITICAL(&mqttStateMux);
    Failsafe::setMqttConnected(connected);
}

bool getMqttConnected()
{
    portENTER_CRITICAL(&mqttStateMux);
    const bool connected = mqttConnected;
    portEXIT_CRITICAL(&mqttStateMux);
    return connected;
}

bool readNumber(const String &json, const char *key, float &value)
{
    const String marker = String("\"") + key + "\":";
    const int start = json.indexOf(marker);
    if (start < 0) return false;
    value = json.substring(start + marker.length()).toFloat();
    return true;
}

bool readText(const String &json, const char *key, char *destination,
              size_t destinationSize)
{
    const String marker = String("\"") + key + "\":\"";
    const int start = json.indexOf(marker);
    if (start < 0) return false;
    const int valueStart = start + marker.length();
    const int valueEnd = json.indexOf('"', valueStart);
    if (valueEnd < 0) return false;
    const String value = json.substring(valueStart, valueEnd);
    strlcpy(destination, value.c_str(), destinationSize);
    return true;
}

void receiveSystemConfig(const String &json)
{
    float day = 0.0f;
    char phase[24]{};
    char plantingDate[11]{};
    char systemStatus[20]{};
    const bool valid =
        readNumber(json, "hari_ke", day) &&
        readText(json, "fase", phase, sizeof(phase)) &&
        readText(json, "tanggal_tanam", plantingDate,
                 sizeof(plantingDate)) &&
        readText(json, "status_sistem", systemStatus,
                 sizeof(systemStatus));

    if (!valid || day < 0.0f || day > 65535.0f) {
        Serial.println("MQTT system config tidak valid");
        return;
    }

    xSemaphoreTake(configMutex, portMAX_DELAY);
    systemConfig.day = static_cast<uint16_t>(day);
    strlcpy(systemConfig.phase, phase, sizeof(systemConfig.phase));
    strlcpy(systemConfig.plantingDate, plantingDate,
            sizeof(systemConfig.plantingDate));
    strlcpy(systemConfig.systemStatus, systemStatus,
            sizeof(systemConfig.systemStatus));
    systemConfigReceived = true;
    systemConfig.received = systemConfigReceived && tdsConfigReceived &&
                            phConfigReceived;
    xSemaphoreGive(configMutex);

    Serial.printf("Config sistem: hari %.0f | %s | %s\n",
                  day, phase, systemStatus);
}

void receiveTdsConfig(const String &json)
{
    float minimum = 0.0f;
    float maximum = 0.0f;
    float target = 0.0f;
    const bool valid = readNumber(json, "tds_min", minimum) &&
                       readNumber(json, "tds_max", maximum) &&
                       readNumber(json, "target_tds", target);
    if (!valid || minimum < 0.0f || minimum > maximum ||
        target < minimum || target > maximum) {
        Serial.println("MQTT config TDS tidak valid");
        return;
    }

    xSemaphoreTake(configMutex, portMAX_DELAY);
    systemConfig.tdsMin = minimum;
    systemConfig.tdsMax = maximum;
    systemConfig.targetTds = target;
    tdsConfigReceived = true;
    systemConfig.received = systemConfigReceived && tdsConfigReceived &&
                            phConfigReceived;
    xSemaphoreGive(configMutex);
    Serial.printf("Config TDS: %.0f-%.0f ppm | target %.0f\n",
                  minimum, maximum, target);
}

void receivePhConfig(const String &json)
{
    float minimum = 0.0f;
    float maximum = 0.0f;
    float target = 0.0f;
    const bool valid = readNumber(json, "ph_min", minimum) &&
                       readNumber(json, "ph_max", maximum) &&
                       readNumber(json, "target_ph", target);
    if (!valid || minimum < 0.0f || maximum > 14.0f ||
        minimum >= maximum || target < minimum || target > maximum) {
        Serial.println("MQTT config pH tidak valid");
        return;
    }

    xSemaphoreTake(configMutex, portMAX_DELAY);
    systemConfig.phMin = minimum;
    systemConfig.phMax = maximum;
    systemConfig.targetPh = target;
    phConfigReceived = true;
    systemConfig.received = systemConfigReceived && tdsConfigReceived &&
                            phConfigReceived;
    xSemaphoreGive(configMutex);
    Serial.printf("Config pH: %.2f-%.2f | target %.2f\n",
                  minimum, maximum, target);
}

void task(void *)
{
    WiFi.mode(WIFI_STA);
    WiFi.persistent(true);
    WiFi.setAutoReconnect(true);

    mqttClient.setServer(Config::Mqtt::HOST, Config::Mqtt::PORT);
    mqttClient.setBufferSize(Config::Mqtt::BUFFER_SIZE);
    wifiManager.setConfigPortalTimeout(Config::Mqtt::WIFI_PORTAL_TIMEOUT_S);

    char clientId[32];
    snprintf(clientId, sizeof(clientId), "%s-%08lX",
             Config::Mqtt::CLIENT_ID_PREFIX,
             static_cast<unsigned long>(ESP.getEfuseMac()));

    while (true) {
        if (WiFi.status() != WL_CONNECTED) {
            setMqttConnected(false);
            NetworkGate::setConnected(false);
            Serial.println(
                "Wi-Fi belum terhubung. Mencoba kredensial tersimpan...");
            if (!wifiManager.autoConnect(Config::Mqtt::WIFI_AP_NAME,
                                         Config::Mqtt::WIFI_AP_PASSWORD)) {
                Serial.println(
                    "Wi-Fi tersimpan gagal dan portal konfigurasi timeout. "
                    "Mencoba lagi...");
                vTaskDelay(pdMS_TO_TICKS(Config::Mqtt::RECONNECT_DELAY_MS));
                continue;
            }
            Serial.print("Wi-Fi terhubung, IP: ");
            Serial.println(WiFi.localIP());
        }

        NetworkGate::setConnected(true);

        if (!mqttClient.connected()) {
            setMqttConnected(false);
            const bool connected = (strlen(Config::Mqtt::USERNAME) > 0)
                ? mqttClient.connect(clientId, Config::Mqtt::USERNAME,
                                     Config::Mqtt::PASSWORD)
                : mqttClient.connect(clientId);
            if (connected) {
                setMqttConnected(true);
                Serial.println("MQTT terhubung");
                Serial.println("MQTT mode: publish telemetry saja");
            } else {
                Serial.printf("MQTT gagal, state: %d\n", mqttClient.state());
                vTaskDelay(pdMS_TO_TICKS(Config::Mqtt::RECONNECT_DELAY_MS));
                continue;
            }
        }

        if (!mqttClient.loop()) {
            setMqttConnected(false);
        }
        Message message{};
        if (xQueueReceive(
                queue, &message,
                pdMS_TO_TICKS(Config::Mqtt::QUEUE_RECEIVE_TIMEOUT_MS))) {
            if (message.topic == Message::Topic::Sensor) {
                if (!mqttClient.publish(Config::Mqtt::SENSOR_TOPIC,
                                        message.payload, true)) {
                    Serial.println("MQTT publish gagal");
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(Config::Mqtt::LOOP_DELAY_MS));
    }
}

}

namespace MqttPublisher {

bool begin()
{
    queue = xQueueCreate(Config::Mqtt::PUBLISH_QUEUE_LENGTH, sizeof(Message));
    configMutex = xSemaphoreCreateMutex();
    return queue != nullptr && configMutex != nullptr &&
           xTaskCreate(task, "MQTT", 8192, nullptr, 1, nullptr) == pdPASS;
}

void publishSensors(const SensorData &data)
{
    if (queue == nullptr) return;
    Message message{};
    message.topic = Message::Topic::Sensor;

    char tdsBuf[16], tempBuf[16], distBuf[16], phUpBuf[8];
    char nutrientABuf[8], nutrientBBuf[8], phDownBuf[8];
    char lightBuf[16], turbVoltageBuf[16], turbStatusBuf[16], phBuf[16];
    char flowRateBuf[16], flowVolumeBuf[16];

    if (data.tdsValid) snprintf(tdsBuf, sizeof(tdsBuf), "%.1f", data.tdsPpm);
    else strlcpy(tdsBuf, "null", sizeof(tdsBuf));

    if (data.temperatureValid) snprintf(tempBuf, sizeof(tempBuf), "%.2f", data.temperatureC);
    else strlcpy(tempBuf, "null", sizeof(tempBuf));

    if (data.distanceValid) snprintf(distBuf, sizeof(distBuf), "%.1f", data.distanceCm);
    else strlcpy(distBuf, "null", sizeof(distBuf));

    if (data.phUpValid) snprintf(phUpBuf, sizeof(phUpBuf), "%d", data.phUpNormal ? 0 : 1);
    else strlcpy(phUpBuf, "null", sizeof(phUpBuf));

    if (data.nutrientAValid) snprintf(nutrientABuf, sizeof(nutrientABuf), "%d", data.nutrientANormal ? 1 : 0);
    else strlcpy(nutrientABuf, "null", sizeof(nutrientABuf));

    if (data.nutrientBValid) snprintf(nutrientBBuf, sizeof(nutrientBBuf), "%d", data.nutrientBNormal ? 1 : 0);
    else strlcpy(nutrientBBuf, "null", sizeof(nutrientBBuf));

    if (data.phDownValid) snprintf(phDownBuf, sizeof(phDownBuf), "%d", data.phDownNormal ? 1 : 0);
    else strlcpy(phDownBuf, "null", sizeof(phDownBuf));

    if (data.lightValid) snprintf(lightBuf, sizeof(lightBuf), "%.1f", data.lightLux);
    else strlcpy(lightBuf, "null", sizeof(lightBuf));

    if (data.turbidityValid) {
        snprintf(turbVoltageBuf, sizeof(turbVoltageBuf), "%.4f", data.turbidityVoltage);
        snprintf(turbStatusBuf, sizeof(turbStatusBuf), "\"%s\"",
                 data.turbidityDirty ? "dirty"
                                     : (data.turbidityCloudy ? "cloudy" : "clear"));
    } else {
        strlcpy(turbVoltageBuf, "null", sizeof(turbVoltageBuf));
        strlcpy(turbStatusBuf, "null", sizeof(turbStatusBuf));
    }

    if (data.phValid) snprintf(phBuf, sizeof(phBuf), "%.3f", data.ph);
    else strlcpy(phBuf, "null", sizeof(phBuf));

    if (data.flowRateValid) snprintf(flowRateBuf, sizeof(flowRateBuf), "%.3f", data.flowRateLpm);
    else strlcpy(flowRateBuf, "null", sizeof(flowRateBuf));

    if (data.flowVolumeValid) snprintf(flowVolumeBuf, sizeof(flowVolumeBuf), "%.3f", data.flowVolumeLiters);
    else strlcpy(flowVolumeBuf, "null", sizeof(flowVolumeBuf));

    const Logic::ActuatorState &actuators = data.actuators;
    snprintf(message.payload, sizeof(message.payload),
             "{\"payload\":[{\"sensors\":{\"water_temp\":%s,\"ph\":%s,\"tds\":%s,"
             "\"turbidity_voltage\":%s,\"turbidity_status\":%s,\"distance\":%s,\"light\":%s,\"ph_up_level\":%s,"
             "\"nutrient_a_level\":%s,\"nutrient_b_level\":%s,\"ph_down_level\":%s,"
             "\"flow_rate_lpm\":%s,\"flow_volume_l\":%s},"
             "\"actuators\":{\"ph_up_pump\":%d,\"ph_down_pump\":%d,\"nutrient_pump\":%d,"
             "\"water_pump\":%d,\"solenoid_valve\":%d,\"grow_light\":%d,\"aerator\":%d},"
             "\"logic\":{\"state\":\"%s\",\"fault_reason\":\"%s\","
             "\"planting_day\":%u,\"planting_week\":%u,\"target_tds\":%.1f}}]}",
             tempBuf, phBuf, tdsBuf, turbVoltageBuf, turbStatusBuf, distBuf,
             lightBuf, phUpBuf, nutrientABuf, nutrientBBuf, phDownBuf,
             flowRateBuf, flowVolumeBuf,
             actuators.phUpPump ? 1 : 0,
             actuators.phDownPump ? 1 : 0,
             actuators.nutrientPump ? 1 : 0,
             actuators.waterPump ? 1 : 0,
             actuators.solenoidValve ? 1 : 0,
             actuators.growLight ? 1 : 0,
             actuators.aerator ? 1 : 0,
             Logic::stateName(actuators.state), actuators.faultReason,
             static_cast<unsigned>(actuators.plantingDay),
             static_cast<unsigned>(actuators.plantingWeek), actuators.targetTds);

    xQueueSend(queue, &message, 0);
}

void publishStatus(uint8_t state)
{
    // Status topic dinonaktifkan sementara
    (void)state;
}

bool getSystemConfig(SystemConfig &config)
{
    if (configMutex == nullptr) return false;
    xSemaphoreTake(configMutex, portMAX_DELAY);
    config = systemConfig;
    xSemaphoreGive(configMutex);
    return config.received;
}

bool isConnected()
{
    return getMqttConnected();
}

bool takeTdsRequest()
{
    portENTER_CRITICAL(&requestMux);
    const bool requested = tdsRequested;
    tdsRequested = false;
    portEXIT_CRITICAL(&requestMux);
    return requested;
}

}
