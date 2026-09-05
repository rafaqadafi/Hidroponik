#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_ADS1X15.h>
#include <OneWire.h>
#include <DallasTemperature.h>

// Ubah pin ini jika sambungan I2C yang digunakan berbeda.
constexpr uint8_t I2C_SDA = 8;
constexpr uint8_t I2C_SCL = 9;
constexpr uint8_t DS18B20_PIN = 4;

constexpr uint8_t ADS1115_ADDRESS = 0x48;
constexpr uint8_t TDS_CHANNEL = 0;       // Sensor TDS terhubung ke A0
constexpr uint8_t MEDIAN_SAMPLE_COUNT = 9; // Harus berjumlah ganjil
constexpr uint8_t AVERAGE_COUNT = 10;
constexpr uint16_t SAMPLE_DELAY_MS = 20;
constexpr uint16_t PRINT_DELAY_MS = 500;

Adafruit_ADS1115 ads;
OneWire oneWire(DS18B20_PIN);
DallasTemperature temperatureSensor(&oneWire);

int16_t readMedianRaw()
{
    int16_t samples[MEDIAN_SAMPLE_COUNT];

    for (uint8_t i = 0; i < MEDIAN_SAMPLE_COUNT; i++) {
        samples[i] = ads.readADC_SingleEnded(TDS_CHANNEL);
        delay(SAMPLE_DELAY_MS);
    }

    // Urutkan sampel dari nilai terkecil ke terbesar.
    for (uint8_t i = 1; i < MEDIAN_SAMPLE_COUNT; i++) {
        const int16_t currentValue = samples[i];
        int8_t position = i - 1;

        while (position >= 0 && samples[position] > currentValue) {
            samples[position + 1] = samples[position];
            position--;
        }

        samples[position + 1] = currentValue;
    }

    return samples[MEDIAN_SAMPLE_COUNT / 2];
}

float readFilteredRaw()
{
    int32_t medianTotal = 0;

    // Rata-ratakan beberapa hasil median agar pembacaan lebih stabil.
    for (uint8_t i = 0; i < AVERAGE_COUNT; i++) {
        medianTotal += readMedianRaw();
    }

    return medianTotal / static_cast<float>(AVERAGE_COUNT);
}

void setup()
{
    Serial.begin(115200);
    delay(1000);

    Wire.begin(I2C_SDA, I2C_SCL);

    if (!ads.begin(ADS1115_ADDRESS, &Wire)) {
        Serial.println(
            "{\"type\":\"status\",\"ads1115\":\"error\"," 
            "\"message\":\"ADS1115 tidak ditemukan\"}"
        );

        while (true) {
            delay(1000);
        }
    }

    // Rentang pengukuran +/-4,096 V dengan resolusi 0,125 mV/bit.
    // Tegangan input tetap tidak boleh melebihi tegangan VDD ADS1115.
    ads.setGain(GAIN_ONE);

    temperatureSensor.begin();
    temperatureSensor.setResolution(12);

    Serial.printf(
        "{\"type\":\"status\",\"ads1115\":\"ready\","
        "\"channel\":0,\"ds18b20_count\":%u}\n",
        temperatureSensor.getDeviceCount()
    );
}

void loop()
{
    float averageRaw = readFilteredRaw();

    // Pembacaan single-ended tidak memiliki tegangan negatif.
    // Hilangkan offset negatif kecil dari ADS1115 di sekitar nol.
    if (averageRaw < 0.0f) {
        averageRaw = 0.0f;
    }

    // Pada GAIN_ONE, satu bit ADS1115 setara dengan 0,125 mV.
    const float averageVoltage = averageRaw * 0.000125f;

    const float averageMillivolts = averageVoltage * 1000.0f;

    temperatureSensor.requestTemperatures();
    const float temperatureC = temperatureSensor.getTempCByIndex(0);

    if (temperatureC == DEVICE_DISCONNECTED_C) {
        Serial.printf(
            "{\"adc_raw\":%.1f,\"millivolt\":%.2f,"
            "\"voltage\":%.4f,\"temperature_c\":null}\n",
            averageRaw,
            averageMillivolts,
            averageVoltage
        );
    } else {
        Serial.printf(
            "{\"adc_raw\":%.1f,\"millivolt\":%.2f,"
            "\"voltage\":%.4f,\"temperature_c\":%.2f}\n",
            averageRaw,
            averageMillivolts,
            averageVoltage,
            temperatureC
        );
    }

    delay(PRINT_DELAY_MS);
}
