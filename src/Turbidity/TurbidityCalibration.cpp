#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_ADS1X15.h>

constexpr uint8_t I2C_SDA = 8;
constexpr uint8_t I2C_SCL = 9;
constexpr uint8_t ADS1115_ADDRESS = 0x48;
constexpr uint8_t TURBIDITY_CHANNEL = 1;  // Turbidity pada A1

constexpr uint8_t MEDIAN_SAMPLE_COUNT = 9;
constexpr uint8_t AVERAGE_COUNT = 10;
constexpr uint16_t SAMPLE_DELAY_MS = 20;
constexpr uint16_t OUTPUT_INTERVAL_MS = 1000;

Adafruit_ADS1115 ads;

int16_t readMedianRaw()
{
    int16_t samples[MEDIAN_SAMPLE_COUNT];

    for (uint8_t i = 0; i < MEDIAN_SAMPLE_COUNT; i++) {
        samples[i] = ads.readADC_SingleEnded(TURBIDITY_CHANNEL);
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

    // Rentang ADC +/-6,144 V; resolusi 0,1875 mV/bit.
    // Tegangan input fisik tetap tidak boleh melebihi VDD ADS1115.
    ads.setGain(GAIN_TWOTHIRDS);

    Serial.println("=== KALIBRASI TURBIDITY ADS1115 A1 ===");
    Serial.println("adc_raw,millivolt,voltage,saturated");
}

void loop()
{
    const float raw = readFilteredRaw();
    const float millivolt = raw * 0.1875f;
    const float voltage = millivolt / 1000.0f;
    const bool saturated = raw >= 32760.0f;

    Serial.printf("%.1f,%.2f,%.4f,%s\n",
                  raw,
                  millivolt,
                  voltage,
                  saturated ? "true" : "false");

    delay(OUTPUT_INTERVAL_MS);
}
