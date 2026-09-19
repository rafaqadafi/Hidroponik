#include <Arduino.h>
#include <Wire.h>
#include <DallasTemperature.h>
#include "ADS1115/Ads1115Manager.h"
#include "TDS/TdsSensor.h"
#include "Temperature/TemperatureSensor.h"
#include "MQTT/MqttPublisher.h"
#include "Relay/Relay.h"
#include "Ultrasonic/UltrasonicSensor.h"
#include "LevelSwitch/LevelSwitch.h"
#include "Flow/FlowSensor.h"
#include "Buzzer/Buzzer.h"
#include "I2C/I2CBus.h"
#include "LightSensor/LightSensor.h"
#include "Turbidity/TurbiditySensor.h"
#include "PH/PhSensor.h"
#include "Config/config.h"
#include "Network/NetworkGate.h"
#include "Display/Display.h"
#include "Failsafe/Failsafe.h"
#include "Logic/Logic.h"

void outputTask(void *)
{
    Failsafe::attachWatchdog();

    TickType_t lastSensorPublish = 0;
    float lastPublishedTds = 0.0f;
    float lastPublishedTemperature = 0.0f;
    float lastPublishedDistance = 0.0f;
    bool lastPublishedPhUp = false;
    bool lastPublishedNutrientA = false;
    bool lastPublishedNutrientB = false;
    bool lastPublishedPhDown = false;
    float lastPublishedFlowRate = 0.0f;
    float lastPublishedFlowVolume = 0.0f;
    float lastPublishedLight = 0.0f;
    float lastPublishedTurbidityVoltage = 0.0f;
    float lastPublishedPh = 0.0f;
    bool sensorsPublished = false;
    bool lastTdsValid = false;
    bool lastTemperatureValid = false;
    bool lastDistanceValid = false;
    bool lastPhUpValid = false;
    bool lastNutrientAValid = false;
    bool lastNutrientBValid = false;
    bool lastPhDownValid = false;
    bool lastFlowRateValid = false;
    bool lastFlowVolumeValid = false;
    bool lastLightValid = false;
    bool lastTurbidityValid = false;
    bool lastTurbidityCloudy = false;
    bool lastTurbidityDirty = false;
    bool lastPhValid = false;
    uint8_t lastActuatorMask = 0;
    Logic::State lastLogicState = Logic::State::Init;
    bool actuatorsPublished = false;
    TickType_t lastSerialPrint = 0;

    while (true) {
        if (!NetworkGate::waitUntilConnected(pdMS_TO_TICKS(1000))) {
            Failsafe::feedWatchdog();
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
            const bool phUpNormal = LevelSwitch::isNormal();
            const bool phUpValid = true;
            const bool nutrientANormal =
                LevelSwitch::isNormal(LevelSwitch::Id::NutrientA);
            const bool nutrientAValid = true;
            const bool nutrientBNormal =
                LevelSwitch::isNormal(LevelSwitch::Id::NutrientB);
            const bool nutrientBValid = true;
            const bool phDownNormal =
                LevelSwitch::isNormal(LevelSwitch::Id::PhDown);
            const bool phDownValid = true;
            Failsafe::updateLocalSafety(
                phUpNormal && nutrientANormal && nutrientBNormal &&
                phDownNormal);
            float flowRateLpm = 0.0f;
            float flowVolumeLiters = 0.0f;
            const bool flowValid = FlowSensor::getReading(
                flowRateLpm, flowVolumeLiters);
            float lightLux = 0.0f;
            const bool lightValid = LightSensor::getLux(lightLux);
            float turbidityVoltage = 0.0f;
            const bool turbidityValid = TurbiditySensor::getVoltage(
                turbidityVoltage);
            const bool turbidityCloudy = turbidityValid &&
                TurbiditySensor::isCloudyWater(turbidityVoltage);
            const bool turbidityDirty = turbidityValid &&
                TurbiditySensor::isDirtyWater(turbidityVoltage);
            const bool phValid = temperatureValid && PhSensor::isReady();
            const float ph = phValid ? PhSensor::getPh(temperature) : 0.0f;

            const Logic::Inputs logicInputs{
                temperature,
                temperatureValid,
                ph,
                phValid,
                ppm,
                tdsValid,
                turbidityVoltage,
                turbidityValid,
                turbidityCloudy,
                turbidityDirty,
                lightLux,
                lightValid,
                distanceCm,
                distanceValid,
                flowRateLpm,
                flowValid,
                phUpNormal,
                nutrientANormal,
                nutrientBNormal,
                phDownNormal,
                false
            };
            const Logic::ActuatorState actuatorState =
                Logic::evaluate(logicInputs);
            Logic::applyActuators(actuatorState);
            Buzzer::setLiquidEmpty(!phUpNormal ||
                                   actuatorState.buzzerTemperature ||
                                   actuatorState.buzzerFault);

            Display::SensorViewData viewData{
                temperature,
                temperatureValid,
                ph,
                phValid,
                ppm,
                tdsValid,
                distanceCm,
                distanceValid,
                phUpNormal,
                phUpValid,
                nutrientANormal,
                nutrientAValid,
                nutrientBNormal,
                nutrientBValid,
                phDownNormal,
                phDownValid,
                lightLux,
                lightValid,
                turbidityVoltage,
                turbidityValid,
                turbidityCloudy,
                turbidityDirty,
                flowRateLpm,
                flowVolumeLiters,
                flowValid,
                NetworkGate::isConnected(),
                MqttPublisher::isConnected()
            };
            Display::update(viewData);

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
            const char *phUpText = phUpNormal ? "OK" : "LOW";
            const char *nutrientAText = nutrientANormal ? "LOW" : "OK";
            const char *nutrientBText = nutrientBNormal ? "LOW" : "OK";
            const char *phDownText = phDownNormal ? "LOW" : "OK";

            const TickType_t now = xTaskGetTickCount();
            if (lastSerialPrint == 0 ||
                now - lastSerialPrint >=
                    pdMS_TO_TICKS(Config::Output::SERIAL_REFRESH_MS)) {
                char phText[16], turbText[16], lightText[16];
                if (phValid) snprintf(phText, sizeof(phText), "%.2f", ph);
                else strlcpy(phText, "ERR", sizeof(phText));
                if (turbidityValid) snprintf(turbText, sizeof(turbText), "%.4f", turbidityVoltage);
                else strlcpy(turbText, "ERR", sizeof(turbText));
                if (lightValid) snprintf(lightText, sizeof(lightText), "%.1f", lightLux);
                else strlcpy(lightText, "ERR", sizeof(lightText));

                const char *turbidityStatus = !turbidityValid
                    ? "ERROR"
                    : (turbidityDirty ? "AIR KOTOR"
                                      : (turbidityCloudy ? "AIR KERUH"
                                                          : "AIR JERNIH"));

                Serial.printf("Suhu: %s C | pH: %s | TDS: %s ppm | Turbidity: %s V (%s) | Jarak: %s cm | Cahaya: %s lux | Float pH Up: %s | Nutrisi A: %s | Nutrisi B: %s | pH Down: %s | Debit: %.2f L/min | Volume: %.3f L\n",
                              temperatureText, phText, tdsText, turbText,
                              turbidityStatus,
                              distanceText, lightText, phUpText,
                              nutrientAText, nutrientBText, phDownText,
                              flowValid ? flowRateLpm : 0.0f,
                              flowValid ? flowVolumeLiters : 0.0f);
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
            const bool phUpChanged =
                (phUpNormal != lastPublishedPhUp);
            const bool nutrientAChanged =
                (nutrientANormal != lastPublishedNutrientA);
            const bool nutrientBChanged =
                (nutrientBNormal != lastPublishedNutrientB);
            const bool phDownChanged =
                (phDownNormal != lastPublishedPhDown);
            const bool flowRateChanged = flowValid &&
                fabsf(flowRateLpm - lastPublishedFlowRate) >=
                    Config::Output::FLOW_RATE_CHANGE_THRESHOLD;
            const bool flowVolumeChanged = flowValid &&
                fabsf(flowVolumeLiters - lastPublishedFlowVolume) >=
                    Config::Output::FLOW_VOLUME_CHANGE_THRESHOLD;
            const bool lightChanged = lightValid &&
                fabsf(lightLux - lastPublishedLight) >=
                    Config::Output::LIGHT_CHANGE_THRESHOLD;
            const bool turbidityChanged = turbidityValid &&
                fabsf(turbidityVoltage - lastPublishedTurbidityVoltage) >=
                    Config::Output::TURBIDITY_VOLTAGE_CHANGE_THRESHOLD;
            const bool turbidityStatusChanged = turbidityValid &&
                ((turbidityCloudy != lastTurbidityCloudy) ||
                 (turbidityDirty != lastTurbidityDirty));
            const bool phChanged = phValid &&
                fabsf(ph - lastPublishedPh) >= Config::Output::PH_CHANGE_THRESHOLD;
            const bool tdsRequested = MqttPublisher::takeTdsRequest();
            const uint8_t currentActuatorMask =
                Logic::actuatorMask(actuatorState);
            const bool actuatorChanged =
                !actuatorsPublished ||
                currentActuatorMask != lastActuatorMask ||
                actuatorState.state != lastLogicState;

            const bool validityChanged = (tdsValid != lastTdsValid) ||
                                         (temperatureValid != lastTemperatureValid) ||
                                         (distanceValid != lastDistanceValid) ||
                                         (phUpValid != lastPhUpValid) ||
                                         (nutrientAValid != lastNutrientAValid) ||
                                         (nutrientBValid != lastNutrientBValid) ||
                                         (phDownValid != lastPhDownValid) ||
                                         (flowValid != lastFlowRateValid) ||
                                         (flowValid != lastFlowVolumeValid) ||
                                         (lightValid != lastLightValid) ||
                                         (turbidityValid != lastTurbidityValid) ||
                                         (phValid != lastPhValid);
            const bool valueChanged = tdsChanged || temperatureChanged ||
                                      distanceChanged || phUpChanged ||
                                      nutrientAChanged || nutrientBChanged ||
                                      phDownChanged || flowRateChanged ||
                                      flowVolumeChanged ||
                                      lightChanged || turbidityChanged ||
                                      turbidityStatusChanged || phChanged ||
                                      actuatorChanged;

            if (!sensorsPublished || tdsRequested || validityChanged ||
                valueChanged ||
                now - lastSensorPublish >=
                    pdMS_TO_TICKS(Config::Output::MQTT_HEARTBEAT_MS)) {
                MqttPublisher::SensorData sensorData{
                    ppm, tdsValid,
                    temperature, temperatureValid,
                    distanceCm, distanceValid,
                    phUpNormal, phUpValid,
                    lightLux, lightValid,
                    turbidityVoltage, turbidityValid,
                    turbidityCloudy, turbidityDirty,
                    ph, phValid,
                    nutrientANormal, nutrientAValid,
                    nutrientBNormal, nutrientBValid,
                    phDownNormal, phDownValid,
                    flowRateLpm, flowValid,
                    flowVolumeLiters, flowValid,
                    actuatorState
                };
                MqttPublisher::publishSensors(sensorData);
                lastPublishedTds = ppm;
                lastPublishedTemperature = temperature;
                lastPublishedDistance = distanceCm;
                lastPublishedPhUp = phUpNormal;
                lastPublishedNutrientA = nutrientANormal;
                lastPublishedNutrientB = nutrientBNormal;
                lastPublishedPhDown = phDownNormal;
                lastPublishedFlowRate = flowRateLpm;
                lastPublishedFlowVolume = flowVolumeLiters;
                lastPublishedLight = lightLux;
                lastPublishedTurbidityVoltage = turbidityVoltage;
                lastPublishedPh = ph;
                lastTdsValid = tdsValid;
                lastTemperatureValid = temperatureValid;
                lastDistanceValid = distanceValid;
                lastPhUpValid = phUpValid;
                lastNutrientAValid = nutrientAValid;
                lastNutrientBValid = nutrientBValid;
                lastPhDownValid = phDownValid;
                lastFlowRateValid = flowValid;
                lastFlowVolumeValid = flowValid;
                lastLightValid = lightValid;
                lastTurbidityValid = turbidityValid;
                lastTurbidityCloudy = turbidityCloudy;
                lastTurbidityDirty = turbidityDirty;
                lastPhValid = phValid;
                lastActuatorMask = currentActuatorMask;
                lastLogicState = actuatorState.state;
                actuatorsPublished = true;
                lastSensorPublish = now;
                sensorsPublished = true;
            }

        Failsafe::feedWatchdog();
        vTaskDelay(pdMS_TO_TICKS(Config::Output::TASK_INTERVAL_MS));
    }
}

void setup()
{
    Serial.begin(115200);
    Wire.begin(Config::Pins::I2C_SDA, Config::Pins::I2C_SCL);

    if (!Display::begin()) {
        Serial.println("ERROR: Layar TFT gagal dimulai");
    } else {
        Serial.println("Display: READY");
    }

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
    if (!LevelSwitch::begin()) {
        Serial.println("ERROR: Modul level switch gagal dimulai");
    }
    if (!Failsafe::begin()) {
        Serial.println("ERROR: Modul failsafe gagal dimulai");
    } else {
        Failsafe::setMqttConnected(MqttPublisher::isConnected());
        Failsafe::setLocalControl(true);
        Serial.println("Failsafe: READY");
    }
    if (!Logic::begin()) {
        Serial.println("ERROR: Modul logic lokal gagal dimulai");
    } else {
        Serial.println("Logic lokal: READY");
    }
    if (!FlowSensor::begin()) {
        Serial.println("ERROR: Modul flow sensor gagal dimulai");
    }
    if (!Buzzer::begin()) {
        Serial.println("ERROR: Modul buzzer gagal dimulai");
    }
    if (!LightSensor::begin()) {
        Serial.println("ERROR: BH1750 tidak ditemukan");
    }

    if (xTaskCreate(outputTask, "Output", 4096, nullptr, 1, nullptr) != pdPASS) {
        Serial.println("ERROR: Task output gagal dibuat");
    }
}

void loop()
{
    NetworkGate::waitUntilConnected();
    vTaskDelay(pdMS_TO_TICKS(Config::Output::TASK_INTERVAL_MS));
}
