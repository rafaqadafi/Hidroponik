#include "MqttPublisher.h"

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <PubSubClient.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include "Relay/Relay.h"
#include "Config/config.h"
#include "OTA/OTAManager.h"
#include "Network/NetworkGate.h"

namespace {

struct Message {
    enum class Topic : uint8_t {
        Tds,
        Temperature,
        Distance,
        RelayStatus,
        Light,
        Turbidity,
        Ph
    } topic;
    // Payload kalibrasi pH memuat beberapa field JSON dan membutuhkan
    // lebih dari 80 karakter.
    char payload[256];
};

WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);
WiFiManager wifiManager;
QueueHandle_t queue = nullptr;
SemaphoreHandle_t configMutex = nullptr;
SystemConfig systemConfig{};
portMUX_TYPE requestMux = portMUX_INITIALIZER_UNLOCKED;
bool tdsRequested = false;
bool systemConfigReceived = false;
bool tdsConfigReceived = false;
bool phConfigReceived = false;

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

void mqttCallback(char *topic, byte *payload, unsigned int length)
{
    char message[256];
    if (length >= sizeof(message)) {
        Serial.println("MQTT payload terlalu panjang");
        return;
    }
    memcpy(message, payload, length);
    message[length] = '\0';

    String command(message);
    command.replace(" ", "");

    if (strcmp(topic, Config::Mqtt::SYSTEM_CONFIG_TOPIC) == 0) {
        receiveSystemConfig(command);
        return;
    }
    if (strcmp(topic, Config::Mqtt::TDS_CONFIG_TOPIC) == 0) {
        receiveTdsConfig(command);
        return;
    }
    if (strcmp(topic, Config::Mqtt::PH_CONFIG_TOPIC) == 0) {
        receivePhConfig(command);
        return;
    }
    if (strcmp(topic, Config::Mqtt::SENSOR_REQUEST_TOPIC) == 0) {
        if (command.indexOf("\"sensor\":\"tds\"") >= 0) {
            portENTER_CRITICAL(&requestMux);
            tdsRequested = true;
            portEXIT_CRITICAL(&requestMux);
            Serial.println("MQTT request pembacaan TDS diterima");
        }
        return;
    }
    if (strcmp(topic, Config::Mqtt::RELAY_COMMAND_TOPIC) != 0) return;

    const int relayKey = command.indexOf("\"relay\":");
    const int stateKey = command.indexOf("\"state\":");
    if (relayKey < 0 || stateKey < 0) {
        Serial.println("MQTT relay command tidak valid");
        return;
    }

    const int relayNumber = command.substring(relayKey + 8).toInt();
    const bool stateOn = command.indexOf("true", stateKey + 8) >= 0;
    const bool stateOff = command.indexOf("false", stateKey + 8) >= 0;
    if ((!stateOn && !stateOff) || relayNumber < 0 ||
        relayNumber > Config::Relay::CHANNEL_COUNT) {
        Serial.println("MQTT relay command tidak valid");
        return;
    }

    const bool accepted = relayNumber == 0
        ? (stateOn ? Relay::allOn() : Relay::allOff())
        : Relay::setRelay(static_cast<uint8_t>(relayNumber), stateOn);
    Serial.println(accepted ? "MQTT relay command diterima"
                            : "MQTT relay command gagal masuk antrean");
}

void task(void *)
{
    WiFi.mode(WIFI_STA);
    WiFi.persistent(true);
    WiFi.setAutoReconnect(true);

    mqttClient.setServer(Config::Mqtt::HOST, Config::Mqtt::PORT);
    mqttClient.setBufferSize(Config::Mqtt::BUFFER_SIZE);
    mqttClient.setCallback(mqttCallback);
    wifiManager.setConfigPortalTimeout(Config::Mqtt::WIFI_PORTAL_TIMEOUT_S);

    char clientId[32];
    snprintf(clientId, sizeof(clientId), "%s-%08lX",
             Config::Mqtt::CLIENT_ID_PREFIX,
             static_cast<unsigned long>(ESP.getEfuseMac()));

    while (true) {
        if (WiFi.status() != WL_CONNECTED) {
            NetworkGate::setConnected(false);
            OTAManager::handle();
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

        if (!OTAManager::begin()) {
            NetworkGate::setConnected(false);
            Serial.println("OTA belum siap, menunggu Wi-Fi...");
            vTaskDelay(pdMS_TO_TICKS(Config::Mqtt::RECONNECT_DELAY_MS));
            continue;
        }
        NetworkGate::setConnected(true);
        OTAManager::handle();

        if (!mqttClient.connected()) {
            if (mqttClient.connect(clientId)) {
                Serial.println("MQTT terhubung");
                mqttClient.subscribe(Config::Mqtt::RELAY_COMMAND_TOPIC);
                mqttClient.subscribe(Config::Mqtt::SYSTEM_CONFIG_TOPIC);
                mqttClient.subscribe(Config::Mqtt::TDS_CONFIG_TOPIC);
                mqttClient.subscribe(Config::Mqtt::PH_CONFIG_TOPIC);
                mqttClient.subscribe(Config::Mqtt::SENSOR_REQUEST_TOPIC);
            } else {
                Serial.printf("MQTT gagal, state: %d\n", mqttClient.state());
                vTaskDelay(pdMS_TO_TICKS(Config::Mqtt::RECONNECT_DELAY_MS));
                continue;
            }
        }

        mqttClient.loop();
        OTAManager::handle();
        Message message{};
        if (xQueueReceive(
                queue, &message,
                pdMS_TO_TICKS(Config::Mqtt::QUEUE_RECEIVE_TIMEOUT_MS))) {
            const char *topic;
            switch (message.topic) {
                case Message::Topic::Tds:
                    topic = Config::Mqtt::TDS_TOPIC;
                    break;
                case Message::Topic::Temperature:
                    topic = Config::Mqtt::TEMPERATURE_TOPIC;
                    break;
                case Message::Topic::Distance:
                    topic = Config::Mqtt::DISTANCE_TOPIC;
                    break;
                case Message::Topic::RelayStatus:
                    topic = Config::Mqtt::RELAY_STATUS_TOPIC;
                    break;
                case Message::Topic::Light:
                    topic = Config::Mqtt::LIGHT_TOPIC;
                    break;
                case Message::Topic::Ph:
                    topic = Config::Mqtt::PH_TOPIC;
                    break;
                default:
                    topic = Config::Mqtt::TURBIDITY_TOPIC;
                    break;
            }
            if (!mqttClient.publish(topic, message.payload, true)) {
                Serial.println("MQTT publish gagal");
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

void publishTds(float ppm, bool valid)
{
    if (queue == nullptr) return;
    Message message{};
    message.topic = Message::Topic::Tds;
    if (valid) snprintf(message.payload, sizeof(message.payload),
                        "{\"tds_ppm\":%.1f}", ppm);
    else snprintf(message.payload, sizeof(message.payload), "{\"tds_ppm\":null}");
    xQueueSend(queue, &message, 0);
}

void publishTemperature(float temperatureC, bool valid)
{
    if (queue == nullptr) return;
    Message message{};
    message.topic = Message::Topic::Temperature;
    if (valid) snprintf(message.payload, sizeof(message.payload),
                        "{\"temperature_c\":%.2f}", temperatureC);
    else snprintf(message.payload, sizeof(message.payload),
                  "{\"temperature_c\":null}");
    xQueueSend(queue, &message, 0);
}

void publishDistance(float distanceCm, bool valid)
{
    if (queue == nullptr) return;
    Message message{};
    message.topic = Message::Topic::Distance;
    if (valid) snprintf(message.payload, sizeof(message.payload),
                        "{\"distance_cm\":%.1f}", distanceCm);
    else snprintf(message.payload, sizeof(message.payload),
                  "{\"distance_cm\":null}");
    xQueueSend(queue, &message, 0);
}

void publishLight(float lux, bool valid)
{
    if (queue == nullptr) return;
    Message message{};
    message.topic = Message::Topic::Light;
    if (valid) snprintf(message.payload, sizeof(message.payload),
                        "{\"light_lux\":%.1f}", lux);
    else snprintf(message.payload, sizeof(message.payload),
                  "{\"light_lux\":null}");
    xQueueSend(queue, &message, 0);
}

void publishTurbidity(float ntu, bool valid)
{
    if (queue == nullptr) return;
    Message message{};
    message.topic = Message::Topic::Turbidity;
    if (valid) {
        snprintf(message.payload, sizeof(message.payload),
                 "{\"turbidity_ntu\":%.2f}", ntu);
    } else {
        snprintf(message.payload, sizeof(message.payload),
                 "{\"turbidity_ntu\":null}");
    }
    xQueueSend(queue, &message, 0);
}

void publishPh(float ph, bool valid)
{
    if (queue == nullptr) return;
    Message message{};
    message.topic = Message::Topic::Ph;
    if (valid) snprintf(message.payload, sizeof(message.payload),
                        "{\"ph\":%.3f}", ph);
    else snprintf(message.payload, sizeof(message.payload), "{\"ph\":null}");
    xQueueSend(queue, &message, 0);
}

void publishRelayState(uint8_t state)
{
    if (queue == nullptr) return;
    Message message{};
    message.topic = Message::Topic::RelayStatus;
    snprintf(
        message.payload, sizeof(message.payload),
        "{\"relay1\":%s,\"relay2\":%s,\"relay3\":%s,\"relay4\":%s,"
        "\"relay5\":%s,\"relay6\":%s,\"relay7\":%s,\"relay8\":%s,"
        "\"state_mask\":%u}",
        (state & (1U << 0)) ? "true" : "false",
        (state & (1U << 1)) ? "true" : "false",
        (state & (1U << 2)) ? "true" : "false",
        (state & (1U << 3)) ? "true" : "false",
        (state & (1U << 4)) ? "true" : "false",
        (state & (1U << 5)) ? "true" : "false",
        (state & (1U << 6)) ? "true" : "false",
        (state & (1U << 7)) ? "true" : "false",
        state);
    xQueueSend(queue, &message, 0);
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
    return mqttClient.connected();
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
