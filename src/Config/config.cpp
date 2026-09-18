#include "config.h"
#include "secrets.h"

namespace Config {

namespace Pins {
// Pin board ESP32-S3 dan perangkat yang terhubung.
const uint8_t I2C_SDA = 8;
const uint8_t I2C_SCL = 9;
const uint8_t DS18B20 = 6;
const uint8_t ULTRASONIC_TRIGGER = 12;
const uint8_t ULTRASONIC_ECHO = 14;
const uint8_t RELAY_SER = 10;
const uint8_t RELAY_RCLK = 18;
const uint8_t RELAY_SRCLK = 11;
const uint8_t LCD_SCLK = 16;
const uint8_t LCD_MOSI = 13;
// Jalur readback ILI9488: TFT SDO/MISO -> GPIO15.
const uint8_t LCD_MISO = 15;
const uint8_t LCD_DC = 5;
const uint8_t LCD_CS = 4;
const uint8_t LCD_RST = 2;
const uint8_t LEVEL_SWITCH = 40;
const uint8_t FLOAT_NUTRIENT_A = 41;
const uint8_t FLOAT_NUTRIENT_B = 42;
const uint8_t FLOAT_PH_DOWN = 39;
const uint8_t FLOW_SENSOR = 1;
const uint8_t BUZZER = 7;
}

namespace LevelSwitch {
const bool ACTIVE_LOW = true;
const uint32_t DEBOUNCE_DELAY_MS = 1000;
}

namespace Buzzer {
const bool ACTIVE_HIGH = true;
}

namespace I2C {
// Alamat perangkat pada bus I2C.
const uint8_t ADS1115_ADDRESS = 0x48;
const uint8_t BH1750_ADDRESS = 0x23;
}

namespace Adc {
// Parameter pembacaan ADS1115. GAIN_ONE menghasilkan 0,125 mV per bit.
const uint8_t MEDIAN_SAMPLE_COUNT = 9;
const uint8_t AVERAGE_COUNT = 10;
const uint16_t SAMPLE_DELAY_MS = 20;
const float LSB_VOLTS_GAIN_ONE = 0.000125f;
}

namespace Tds {
const uint8_t ADC_CHANNEL = 0;
const uint32_t SAMPLE_INTERVAL_MS = 500;
}

namespace Ph {
const uint8_t ADC_CHANNEL = 2;
const uint32_t SAMPLE_INTERVAL_MS = 1000;
const float MIN_VALID_VOLTAGE = 0.01f;
const float MAX_VALID_VOLTAGE = 4.09f;
}

namespace Turbidity {
const uint8_t ADC_CHANNEL = 1;
const uint32_t SAMPLE_INTERVAL_MS = 1000;
const float MIN_VALID_VOLTAGE = 0.01f;
const float MAX_VALID_VOLTAGE = 4.09f;
// Plateau air jernih yang diamati di COM7: 2.9529-2.9537 V.
// Nilai rata-rata tersimpan sebagai referensi threshold air jernih.
const float CLEAR_WATER_THRESHOLD_VOLTAGE = 2.9533f;
// Plateau air keruh yang diamati di COM7: 2.5895-2.6085 V.
// Nilai ini menggantikan catatan sebelumnya; nilai NTU belum ditentukan.
const float CLOUDY_WATER_REFERENCE_VOLTAGE = 2.6015f;
}

namespace Temperature {
const uint32_t SAMPLE_INTERVAL_MS = 1000;
}

namespace Ultrasonic {
// Nilai input dan validasi HC-SR04.
const uint16_t TRIGGER_PULSE_US = 150;
const uint16_t TRIGGER_SETTLE_US = 100;
const uint32_t ECHO_TIMEOUT_US = 200000;
const uint32_t ECHO_WAIT_TIMEOUT_MS = 125;
const uint32_t SAMPLE_INTERVAL_MS = 1000;
const uint32_t STALE_TIMEOUT_MS = 5000;
const uint8_t MEDIAN_WINDOW = 7;
}

namespace Flow {
// Berdasarkan datasheet YF-S201: F = 7,5 x Q dan sekitar 450 pulsa/liter.
const float PULSES_PER_LITER = 450.0f;
const uint32_t SAMPLE_INTERVAL_MS = 1000;
}

namespace Light {
const uint8_t POWER_ON_COMMAND = 0x01;
const uint8_t CONTINUOUS_HIGH_RES_MODE = 0x10;
const uint32_t SAMPLE_INTERVAL_MS = 2000;
// Kalibrasi satu titik: 147 lux terbaca sebagai 365 lux pada lux meter referensi.
const float LUX_DIVISOR = 0.4833f;
}

namespace Display {
// Kecepatan lebih rendah membantu kestabilan SPI pada kabel TFT yang panjang.
const uint32_t SPI_WRITE_FREQUENCY_HZ = 20000000;
// Readback lebih lambat agar pemeriksaan status controller stabil.
const uint32_t SPI_READ_FREQUENCY_HZ = 8000000;
// Pemeriksaan status ringan; recovery hanya dilakukan jika readback gagal.
const uint32_t HEALTH_CHECK_INTERVAL_MS = 5000;
// TFT tanpa touch: halaman angka dan grafik berganti otomatis.
const uint32_t PAGE_ROTATION_INTERVAL_MS = 10000;
}

namespace Relay {
const bool ACTIVE_LOW = true;
const uint8_t CHANNEL_COUNT = 8;
const uint8_t ALL_ON_MASK = 0xFF;
const uint8_t COMMAND_QUEUE_LENGTH = 16;
}

namespace Output {
const float TDS_CHANGE_THRESHOLD = 0.1f;
const float TEMPERATURE_CHANGE_THRESHOLD = 0.01f;
const float DISTANCE_CHANGE_THRESHOLD = 0.5f;
const float LIGHT_CHANGE_THRESHOLD = 10.0f;
const float TURBIDITY_VOLTAGE_CHANGE_THRESHOLD = 0.01f;
const float PH_CHANGE_THRESHOLD = 0.01f;
const float FLOW_RATE_CHANGE_THRESHOLD = 0.05f;
const float FLOW_VOLUME_CHANGE_THRESHOLD = 0.01f;
const uint32_t MQTT_HEARTBEAT_MS = 60000;
const uint32_t SERIAL_REFRESH_MS = 5000;
const uint32_t TASK_INTERVAL_MS = 1000;
}

namespace Mqtt {
const char HOST[] = "192.168.1.75";
const uint16_t PORT = 1883;
const char SENSOR_TOPIC[] = "farming/ESP32-HYDROPONIC-01/hydroponic/data";
const char CONTROL_TOPIC[] = "farming/ESP32-HYDROPONIC-01/hydroponic/control";
// Topik konfigurasi dinonaktifkan sementara:
// const char SYSTEM_CONFIG_TOPIC[] = "sumenep/hydroponic/system/config";
// const char TDS_CONFIG_TOPIC[] = "sumenep/hydroponic/config/tds";
// const char PH_CONFIG_TOPIC[] = "sumenep/hydroponic/config/ph";
// const char SENSOR_REQUEST_TOPIC[] = "sumenep/hydroponic/sensor/request";
const char USERNAME[] = HYDRO_MQTT_USERNAME;
const char PASSWORD[] = HYDRO_MQTT_PASSWORD;
const char WIFI_AP_NAME[] = "TDS-MQTT-Setup";
const char WIFI_AP_PASSWORD[] = HYDRO_WIFI_AP_PASSWORD;
const char CLIENT_ID_PREFIX[] = "ESP32-TDS";
const uint16_t BUFFER_SIZE = 512;
const uint16_t WIFI_PORTAL_TIMEOUT_S = 180;
const uint8_t PUBLISH_QUEUE_LENGTH = 8;
const uint16_t QUEUE_RECEIVE_TIMEOUT_MS = 100;
const uint16_t LOOP_DELAY_MS = 20;
const uint16_t RECONNECT_DELAY_MS = 5000;
}

}
