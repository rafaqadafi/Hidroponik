#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_ADS1X15.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include "MQTT/MqttPublisher.h"

constexpr uint8_t I2C_SDA = 8;
constexpr uint8_t I2C_SCL = 9;
constexpr uint8_t DS18B20_PIN = 6;
constexpr uint8_t ADS1115_ADDRESS = 0x48;
constexpr uint8_t PH_CHANNEL = 2;  // Sensor pH pada ADS1115 A2

constexpr uint8_t MEDIAN_SAMPLE_COUNT = 9;
constexpr uint8_t AVERAGE_COUNT = 10;
constexpr uint16_t SAMPLE_DELAY_MS = 20;
constexpr uint16_t OUTPUT_INTERVAL_MS = 1000;

Adafruit_ADS1115 ads;
OneWire oneWire(DS18B20_PIN);
DallasTemperature temperatureSensor(&oneWire);

struct PhCalibrationPoint {
    float voltage;
    float temperatureC;
    float referencePh;
};

// Hasil pengukuran tiga buffer, dari tegangan rendah ke tinggi.
constexpr PhCalibrationPoint PH_CALIBRATION_POINTS[] = {
    {1.1648f, 26.62f, 9.1670f},
    {1.5460f, 26.77f, 6.8565f},
    {2.0396f, 26.47f, 4.0100f}
};

constexpr float CALIBRATION_TEMPERATURE_C = 26.62f;

float interpolatePh(float voltage,
                    const PhCalibrationPoint &a,
                    const PhCalibrationPoint &b)
{
    return a.referencePh + (voltage - a.voltage) *
           (b.referencePh - a.referencePh) / (b.voltage - a.voltage);
}

float voltageToPh(float voltage, float temperatureC)
{
    const PhCalibrationPoint &alkaline = PH_CALIBRATION_POINTS[0];
    const PhCalibrationPoint &neutral = PH_CALIBRATION_POINTS[1];
    const PhCalibrationPoint &acid = PH_CALIBRATION_POINTS[2];

    // Interpolasi per segmen membuat ketiga titik kalibrasi tetap tepat.
    const float phAtCalibrationTemperature =
        voltage <= neutral.voltage
            ? interpolatePh(voltage, alkaline, neutral)
            : interpolatePh(voltage, neutral, acid);

    // Kompensasi perubahan kemiringan elektroda terhadap suhu absolut.
    return 7.0f + (phAtCalibrationTemperature - 7.0f) *
                  (CALIBRATION_TEMPERATURE_C + 273.15f) /
                  (temperatureC + 273.15f);
}

int16_t readMedianRaw()
{
    int16_t samples[MEDIAN_SAMPLE_COUNT];

    for (uint8_t i = 0; i < MEDIAN_SAMPLE_COUNT; i++) {
        samples[i] = ads.readADC_SingleEnded(PH_CHANNEL);
        delay(SAMPLE_DELAY_MS);
    }

    for (uint8_t i = 1; i < MEDIAN_SAMPLE_COUNT; i++) {
        const int16_t current = samples[i];
        int8_t position = i - 1;
        while (position >= 0 && samples[position] > current) {
            samples[position + 1] = samples[position];
            position--;
        }
        samples[position + 1] = current;
    }

    return samples[MEDIAN_SAMPLE_COUNT / 2];
}

float readFilteredRaw()
{
    int32_t total = 0;
    for (uint8_t i = 0; i < AVERAGE_COUNT; i++) {
        total += readMedianRaw();
    }
    return total / static_cast<float>(AVERAGE_COUNT);
}

void setup()
{
    Serial.begin(115200);
    delay(1000);

    Wire.begin(I2C_SDA, I2C_SCL);
    if (!ads.begin(ADS1115_ADDRESS, &Wire)) {
        Serial.println("ERROR: ADS1115 tidak ditemukan pada alamat 0x48");
        while (true) delay(1000);
    }

    // Rentang +/-4,096 V dan resolusi 0,125 mV/bit.
    ads.setGain(GAIN_ONE);

    temperatureSensor.begin();
    temperatureSensor.setResolution(12);

    if (!MqttPublisher::begin()) {
        Serial.println("ERROR: Modul MQTT gagal dimulai");
    }

    Serial.println("=== SENSOR pH TERKALIBRASI ===");
    Serial.printf("DS18B20 ditemukan: %u\n",
                  temperatureSensor.getDeviceCount());
    Serial.println("pH | SUHU | VOLTAGE | ADC RAW");
}

void loop()
{
    const float raw = readFilteredRaw();
    const float millivolt = raw * 0.125f;
    const float voltage = millivolt / 1000.0f;

    temperatureSensor.requestTemperatures();
    const float temperatureC = temperatureSensor.getTempCByIndex(0);
    const bool temperatureValid =
        temperatureC != DEVICE_DISCONNECTED_C &&
        temperatureC >= -55.0f && temperatureC <= 125.0f;
    const bool saturated = raw >= 32760.0f;

    if (temperatureValid) {
        const float ph = voltageToPh(voltage, temperatureC);
        Serial.printf(
            "%.3f | %.2f C | %.4f V | %.1f\n",
            ph, temperatureC, voltage, raw);
        MqttPublisher::publishPh(ph, !saturated);
        MqttPublisher::publishTemperature(temperatureC, true);
    } else {
        Serial.printf("ERROR | ERROR | %.4f V | %.1f\n", voltage, raw);
        MqttPublisher::publishPh(0.0f, false);
        MqttPublisher::publishTemperature(0.0f, false);
    }

    delay(OUTPUT_INTERVAL_MS);
}
