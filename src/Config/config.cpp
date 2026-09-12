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
const uint8_t LCD_DC = 5;
const uint8_t LCD_CS = 4;
const uint8_t LCD_RST = 2;
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
}

namespace Temperature {
const uint32_t SAMPLE_INTERVAL_MS = 1000;
}

namespace Ultrasonic {
// Nilai input dan validasi HC-SR04.
const uint16_t TRIGGER_PULSE_US = 150;
const uint16_t TRIGGER_SETTLE_US = 100;
const uint32_t ECHO_TIMEOUT_US = 120000;
const uint32_t ECHO_WAIT_TIMEOUT_MS = 125;
const uint32_t SAMPLE_INTERVAL_MS = 1000;
const uint32_t STALE_TIMEOUT_MS = 5000;
const uint8_t MEDIAN_WINDOW = 7;
}

namespace Light {
const uint8_t POWER_ON_COMMAND = 0x01;
const uint8_t CONTINUOUS_HIGH_RES_MODE = 0x10;
const uint32_t SAMPLE_INTERVAL_MS = 2000;
const float LUX_DIVISOR = 1.2f;
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
const float TURBIDITY_CHANGE_THRESHOLD = 0.5f;
const float PH_CHANGE_THRESHOLD = 0.01f;
const uint32_t MQTT_HEARTBEAT_MS = 60000;
const uint32_t SERIAL_REFRESH_MS = 5000;
const uint32_t TASK_INTERVAL_MS = 1000;
}

namespace Mqtt {
const char HOST[] = "192.168.1.2";
const uint16_t PORT = 1883;
const char SENSOR_TOPIC[] = "sumenep/hydroponic/sensor";
const char STATUS_TOPIC[] = "sumenep/hydroponic/status";
const char CONTROL_TOPIC[] = "sumenep/hydroponic/control";
const char SYSTEM_CONFIG_TOPIC[] = "sumenep/hydroponic/system/config";
const char TDS_CONFIG_TOPIC[] = "sumenep/hydroponic/config/tds";
const char PH_CONFIG_TOPIC[] = "sumenep/hydroponic/config/ph";
const char SENSOR_REQUEST_TOPIC[] = "sumenep/hydroponic/sensor/request";
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
