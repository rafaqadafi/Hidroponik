#include <Arduino.h>
#include <Wire.h>
#include <DallasTemperature.h>
#include "ADS1115/Ads1115Manager.h"
#include "TDS/TdsSensor.h"
#include "Temperature/TemperatureSensor.h"
#include "MQTT/MqttPublisher.h"
#include "Relay/Relay.h"
#include "Ultrasonic/UltrasonicSensor.h"
#include "I2C/I2CBus.h"
#include "LightSensor/LightSensor.h"
#include "Turbidity/TurbiditySensor.h"
#include "PH/PhSensor.h"
#include "Config/config.h"
#include "Network/NetworkGate.h"

void outputTask(void *)
{
    TickType_t lastSensorPublish = 0;
    TickType_t lastStatusPublish = 0;
    float lastPublishedTds = 0.0f;
    float lastPublishedTemperature = 0.0f;
    float lastPublishedDistance = 0.0f;
    float lastPublishedLight = 0.0f;
    float lastPublishedTurbidity = 0.0f;
    float lastPublishedPh = 0.0f;
    bool sensorsPublished = false;
    bool lastTdsValid = false;
    bool lastTemperatureValid = false;
    bool lastDistanceValid = false;
    bool lastLightValid = false;
    bool lastTurbidityValid = false;
    bool lastPhValid = false;
    uint8_t lastRelayState = 0;
    bool relayStatePublished = false;
    TickType_t lastSerialPrint = 0;

    while (true) {
        if (!NetworkGate::waitUntilConnected()) {
            vTaskDelay(pdMS_TO_TICKS(Config::Output::TASK_INTERVAL_MS));
            continue;
        }

        const bool temperatureReady = TemperatureSensor::isReady();
        const float temperature = temperatureReady
            ? TemperatureSensor::getCelsius()
            : DEVICE_DISCONNECTED_C;
        const bool temperatureValid = temperature != DEVICE_DISCONNECTED_C;
        const bool tdsValid = temperatureValid && TdsSensor::isReady();
        const float ppm = tdsValid ? TdsSensor::getPPM(temperature) : 0.0f;

            float distanceCm = 0.0f;
            const bool distanceValid =
                UltrasonicSensor::getDistanceCm(distanceCm);
            float lightLux = 0.0f;
            const bool lightValid = LightSensor::getLux(lightLux);
            float turbidityNtu = 0.0f;
            float turbidityVoltage = 0.0f;
            const bool turbidityValid = TurbiditySensor::getReading(
                turbidityNtu, turbidityVoltage);
            const bool phValid = temperatureValid && PhSensor::isReady();
            const float ph = phValid ? PhSensor::getPh(temperature) : 0.0f;

            char tdsText[16];
            char temperatureText[16];
            char distanceText[16];
            if (tdsValid) snprintf(tdsText, sizeof(tdsText), "%.1f", ppm);
            else strlcpy(tdsText, "ERROR", sizeof(tdsText));
            if (temperatureValid) {
                snprintf(temperatureText, sizeof(temperatureText), "%.2f",
                         temperature);
            } else {
                strlcpy(temperatureText, "ERROR", sizeof(temperatureText));
            }
            if (distanceValid) {
                snprintf(distanceText, sizeof(distanceText), "%.1f", distanceCm);
            } else {
                snprintf(distanceText, sizeof(distanceText), "ERR(%lu)",
                         static_cast<unsigned long>(
                             UltrasonicSensor::getLastEchoDurationUs()));
            }

            SystemConfig config{};
            const bool configValid = MqttPublisher::getSystemConfig(config);
            char dayText[8];
            if (configValid) snprintf(dayText, sizeof(dayText), "%u", config.day);
            else strlcpy(dayText, "--", sizeof(dayText));

            const TickType_t now = xTaskGetTickCount();
            if (lastSerialPrint == 0 ||
                now - lastSerialPrint >=
                    pdMS_TO_TICKS(Config::Output::SERIAL_REFRESH_MS)) {
                const uint8_t relayState = Relay::getState();
                const char *relay3Text = (relayState & (1U << 2)) ? "ON" : "OFF";
                const char *relay4Text = (relayState & (1U << 3)) ? "ON" : "OFF";
                if (configValid) {
                    Serial.printf(
                        "TDS:%s ppm | Batas:%.0f-%.0f | Target:%.0f ppm | "
                        "Suhu:%s C | Jarak:%s cm | Cahaya:%.1f lux | "
                        "Turbidity:%.2f NTU (%.4f V) | pH:%.3f "
                        "[%.2f-%.2f target %.2f] | R3:%s | R4:%s | "
                        "Relay:0x%02X | "
                        "MQTT:%s | Hari:%s | Fase:%s\n",
                        tdsText,
                        config.tdsMin,
                        config.tdsMax,
                        config.targetTds,
                        temperatureText,
                        distanceText,
                        lightValid ? lightLux : -1.0f,
                        turbidityValid ? turbidityNtu : -1.0f,
                        turbidityValid ? turbidityVoltage : -1.0f,
                        phValid ? ph : -1.0f,
                        config.phMin,
                        config.phMax,
                        config.targetPh,
                        relay3Text,
                        relay4Text,
                        relayState,
                        MqttPublisher::isConnected() ? "ON" : "OFF",
                        dayText,
                        config.phase
                    );
                } else {
                    Serial.printf(
                        "TDS:%s ppm | Batas:-- | Target:-- | Suhu:%s C | "
                        "Jarak:%s cm | Cahaya:%.1f lux | "
                        "Turbidity:%.2f NTU (%.4f V) | pH:%.3f | "
                        "R3:%s | R4:%s | Relay:0x%02X | MQTT:%s | "
                        "Hari:-- | Fase:--\n",
                        tdsText,
                        temperatureText,
                        distanceText,
                        lightValid ? lightLux : -1.0f,
                        turbidityValid ? turbidityNtu : -1.0f,
                        turbidityValid ? turbidityVoltage : -1.0f,
                        phValid ? ph : -1.0f,
                        relay3Text,
                        relay4Text,
                        relayState,
                        MqttPublisher::isConnected() ? "ON" : "OFF"
                    );
                }
                lastSerialPrint = now;
            }

            const bool tdsChanged = tdsValid &&
                fabsf(ppm - lastPublishedTds) >=
                    Config::Output::TDS_CHANGE_THRESHOLD;
            const bool temperatureChanged = temperatureValid &&
                fabsf(temperature - lastPublishedTemperature) >=
                    Config::Output::TEMPERATURE_CHANGE_THRESHOLD;
            const bool distanceChanged = distanceValid &&
                fabsf(distanceCm - lastPublishedDistance) >=
                    Config::Output::DISTANCE_CHANGE_THRESHOLD;
            const bool lightChanged = lightValid &&
                fabsf(lightLux - lastPublishedLight) >=
                    Config::Output::LIGHT_CHANGE_THRESHOLD;
            const bool turbidityChanged = turbidityValid &&
                fabsf(turbidityNtu - lastPublishedTurbidity) >=
                    Config::Output::TURBIDITY_CHANGE_THRESHOLD;
            const bool phChanged = phValid &&
                fabsf(ph - lastPublishedPh) >= Config::Output::PH_CHANGE_THRESHOLD;
            const bool tdsRequested = MqttPublisher::takeTdsRequest();

            const bool validityChanged = (tdsValid != lastTdsValid) ||
                                         (temperatureValid != lastTemperatureValid) ||
                                         (distanceValid != lastDistanceValid) ||
                                         (lightValid != lastLightValid) ||
                                         (turbidityValid != lastTurbidityValid) ||
                                         (phValid != lastPhValid);
            const bool valueChanged = tdsChanged || temperatureChanged ||
                                      distanceChanged || lightChanged ||
                                      turbidityChanged || phChanged;

            if (!sensorsPublished || tdsRequested || validityChanged ||
                valueChanged ||
                now - lastSensorPublish >=
                    pdMS_TO_TICKS(Config::Output::MQTT_HEARTBEAT_MS)) {
                MqttPublisher::SensorData sensorData{
                    ppm, tdsValid,
                    temperature, temperatureValid,
                    distanceCm, distanceValid,
                    lightLux, lightValid,
                    turbidityNtu, turbidityValid,
                    ph, phValid
                };
                MqttPublisher::publishSensors(sensorData);
                lastPublishedTds = ppm;
                lastPublishedTemperature = temperature;
                lastPublishedDistance = distanceCm;
                lastPublishedLight = lightLux;
                lastPublishedTurbidity = turbidityNtu;
                lastPublishedPh = ph;
                lastTdsValid = tdsValid;
                lastTemperatureValid = temperatureValid;
                lastDistanceValid = distanceValid;
                lastLightValid = lightValid;
                lastTurbidityValid = turbidityValid;
                lastPhValid = phValid;
                lastSensorPublish = now;
                sensorsPublished = true;
            }

        // Status relay harus tetap dipublikasikan meskipun sensor belum siap.
        if (Relay::isReady()) {
            const uint8_t relayState = Relay::getState();
            if (!relayStatePublished || relayState != lastRelayState ||
                now - lastStatusPublish >=
                    pdMS_TO_TICKS(Config::Output::MQTT_HEARTBEAT_MS)) {
                MqttPublisher::publishStatus(relayState);
                lastRelayState = relayState;
                lastStatusPublish = now;
                relayStatePublished = true;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(Config::Output::TASK_INTERVAL_MS));
    }
}

void setup()
{
    Serial.begin(115200);
    Wire.begin(Config::Pins::I2C_SDA, Config::Pins::I2C_SCL);

    if (!I2CBus::begin()) {
        Serial.println("ERROR: Mutex I2C gagal dibuat");
    }

    if (!NetworkGate::begin()) {
        Serial.println("ERROR: Gerbang jaringan gagal dibuat");
        while (true) vTaskDelay(pdMS_TO_TICKS(1000));
    }

    // Wi-FiManager harus selesai menghubungkan Wi-Fi sebelum task lain dibuat.
    if (!MqttPublisher::begin()) {
        Serial.println("ERROR: Modul MQTT gagal dimulai");
        while (true) vTaskDelay(pdMS_TO_TICKS(1000));
    }
    Serial.println("Menunggu Wi-Fi tersambung melalui WiFiManager...");
    while (!NetworkGate::waitUntilConnected()) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    Serial.println("Wi-Fi siap, task sensor mulai dijalankan");

    if (!Ads1115Manager::begin()) {
        Serial.println(
            "ERROR: ADS1115 tidak ditemukan; sensor ADS dinonaktifkan");
    }

    if (!TdsSensor::begin()) Serial.println("ERROR: Modul TDS gagal dimulai");
    if (!TurbiditySensor::begin()) {
        Serial.println("ERROR: Modul turbidity gagal dimulai");
    }
    if (!PhSensor::begin()) Serial.println("ERROR: Modul pH gagal dimulai");
    if (!TemperatureSensor::begin()) Serial.println("ERROR: Modul suhu gagal dimulai");
    if (Relay::begin()) Serial.println("Relay Status: READY");
    else Serial.println("ERROR: Modul relay gagal dimulai");
    if (!UltrasonicSensor::begin()) {
        Serial.println("ERROR: Modul ultrasonik gagal dimulai");
    }
    if (!LightSensor::begin()) {
        Serial.println("ERROR: BH1750 tidak ditemukan");
    }

    if (xTaskCreate(outputTask, "Output", 3072, nullptr, 1, nullptr) != pdPASS) {
        Serial.println("ERROR: Task output gagal dibuat");
    }
}

void loop()
{
    NetworkGate::waitUntilConnected();
    vTaskDelay(pdMS_TO_TICKS(Config::Output::TASK_INTERVAL_MS));
}
