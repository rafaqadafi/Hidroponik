#pragma once

#include <stdint.h>

// Ubah nilai konfigurasi pada config.cpp, lalu build dan upload ulang firmware.
namespace Config {

namespace Pins {
extern const uint8_t I2C_SDA;
extern const uint8_t I2C_SCL;
extern const uint8_t DS18B20;
extern const uint8_t ULTRASONIC_TRIGGER;
extern const uint8_t ULTRASONIC_ECHO;
extern const uint8_t RELAY_SER;
extern const uint8_t RELAY_RCLK;
extern const uint8_t RELAY_SRCLK;
extern const uint8_t LCD_SCLK;
extern const uint8_t LCD_MOSI;
extern const uint8_t LCD_DC;
extern const uint8_t LCD_CS;
extern const uint8_t LCD_RST;
}

namespace I2C {
extern const uint8_t ADS1115_ADDRESS;
extern const uint8_t BH1750_ADDRESS;
}

namespace Adc {
extern const uint8_t MEDIAN_SAMPLE_COUNT;
extern const uint8_t AVERAGE_COUNT;
extern const uint16_t SAMPLE_DELAY_MS;
extern const float LSB_VOLTS_GAIN_ONE;
}

namespace Tds {
extern const uint8_t ADC_CHANNEL;
extern const uint32_t SAMPLE_INTERVAL_MS;
}

namespace Ph {
extern const uint8_t ADC_CHANNEL;
extern const uint32_t SAMPLE_INTERVAL_MS;
extern const float MIN_VALID_VOLTAGE;
extern const float MAX_VALID_VOLTAGE;
}

namespace Turbidity {
extern const uint8_t ADC_CHANNEL;
extern const uint32_t SAMPLE_INTERVAL_MS;
extern const float MIN_VALID_VOLTAGE;
extern const float MAX_VALID_VOLTAGE;
}

namespace Temperature {
extern const uint32_t SAMPLE_INTERVAL_MS;
}

namespace Ultrasonic {
// Input sensor HC-SR04: pulsa trigger dan batas pembacaan echo.
extern const uint16_t TRIGGER_PULSE_US;
extern const uint16_t TRIGGER_SETTLE_US;
extern const uint32_t ECHO_TIMEOUT_US;
extern const uint32_t ECHO_WAIT_TIMEOUT_MS;
extern const uint32_t SAMPLE_INTERVAL_MS;
extern const uint32_t STALE_TIMEOUT_MS;
extern const uint8_t MEDIAN_WINDOW;
}

namespace Light {
extern const uint8_t POWER_ON_COMMAND;
extern const uint8_t CONTINUOUS_HIGH_RES_MODE;
extern const uint32_t SAMPLE_INTERVAL_MS;
extern const float LUX_DIVISOR;
}

namespace Relay {
extern const bool ACTIVE_LOW;
extern const uint8_t CHANNEL_COUNT;
extern const uint8_t ALL_ON_MASK;
extern const uint8_t COMMAND_QUEUE_LENGTH;
}

namespace Output {
extern const float TDS_CHANGE_THRESHOLD;
extern const float TEMPERATURE_CHANGE_THRESHOLD;
extern const float DISTANCE_CHANGE_THRESHOLD;
extern const float LIGHT_CHANGE_THRESHOLD;
extern const float TURBIDITY_CHANGE_THRESHOLD;
extern const float PH_CHANGE_THRESHOLD;
extern const uint32_t MQTT_HEARTBEAT_MS;
extern const uint32_t SERIAL_REFRESH_MS;
extern const uint32_t TASK_INTERVAL_MS;
}

namespace Mqtt {
extern const char HOST[];
extern const uint16_t PORT;
extern const char SENSOR_TOPIC[];
extern const char STATUS_TOPIC[];
extern const char CONTROL_TOPIC[];
extern const char SYSTEM_CONFIG_TOPIC[];
extern const char TDS_CONFIG_TOPIC[];
extern const char PH_CONFIG_TOPIC[];
extern const char SENSOR_REQUEST_TOPIC[];
extern const char WIFI_AP_NAME[];
extern const char WIFI_AP_PASSWORD[];
extern const char CLIENT_ID_PREFIX[];
extern const uint16_t BUFFER_SIZE;
extern const uint16_t WIFI_PORTAL_TIMEOUT_S;
extern const uint8_t PUBLISH_QUEUE_LENGTH;
extern const uint16_t QUEUE_RECEIVE_TIMEOUT_MS;
extern const uint16_t LOOP_DELAY_MS;
extern const uint16_t RECONNECT_DELAY_MS;
}

}
