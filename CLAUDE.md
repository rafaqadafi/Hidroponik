# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

This is an ESP32-S3 hydroponic monitoring system built with PlatformIO and Arduino framework. It reads multiple sensors (TDS, pH, temperature, turbidity, ultrasonic distance, light) via ADS1115 ADC and DS18B20, publishes data to MQTT (broker.emqx.io), and controls relays for nutrient/pH dosing. Node-RED handles automation logic.

## Build & Development Commands

```bash
# Build firmware
pio run

# Upload to ESP32-S3 (COM7)
pio run -t upload

# Serial monitor (115200 baud)
pio device monitor

# Clean build
pio run -t clean
```

## Architecture

### Hardware
- **MCU**: ESP32-S3 DevKitC-1 (16MB Flash + 8MB PSRAM)
- **I2C**: SDA=GPIO 8, SCL=GPIO 9
- **ADC**: ADS1115 (I2C, address 0x48, GAIN_ONE ±4.096V) - ch0=TDS, ch1=turbidity, ch2=pH
- **Temp**: DS18B20 (OneWire on GPIO 4)
- **Ultrasonic**: HC-SR04 style, trigger=GPIO 12, echo=GPIO 14 (interrupt-driven)
- **Light**: BH1750 (I2C)
- **Relay**: 8-channel via 74HC595 shift register (SER=10, RCLK=18, SRCLK=11), active-low

### Firmware Structure (FreeRTOS tasks)

| Component | File | Responsibility |
|-----------|------|----------------|
| `main.cpp` | Entry point, creates `outputTask` (1s loop) | Reads all sensors, publishes on change + 60s heartbeat |
| `Ads1115Manager` | `src/ADS1115/` | I2C mutex + filtered voltage reads per channel |
| `I2CBus` | `src/I2C/` | FreeRTOS mutex for shared I2C bus |
| `TdsSensor` | `src/TDS/` | Reads ADS1115 ch0, temp-compensated PPM |
| `PhSensor` | `src/PH/` | Reads ADS1115 ch1, temp-compensated pH |
| `TurbiditySensor` | `src/Turbidity/` | Reads ADS1115 ch2, polynomial NTU calc |
| `TemperatureSensor` | `src/Temperature/` | DS18B20 wrapper |
| `UltrasonicSensor` | `src/Ultrasonic/` | Distance measurement |
| `LightSensor` | `src/LightSensor/` | BH1750 lux reading |
| `Relay` | `src/Relay/` | 8-relay control via 74HC595 shift register + command queue |
| `NetworkGate` | `src/Network/` | FreeRTOS event gate; sensor, relay, and output tasks wait for Wi-Fi + OTA |
| `OTAManager` | `src/OTA/` | Arduino OTA service handled by the Wi-Fi/MQTT task |
| `MqttPublisher` | `src/MQTT/` | FreeRTOS task: WiFiManager + PubSubClient, queued publishes |

### Key Design Patterns

1. **Static namespaces** - Each sensor is a C++ namespace with `begin()`, `isReady()`, getter methods
2. **Per-sensor FreeRTOS task + mutex** - Each sensor task samples its channel periodically; getters lock a mutex to read latest values
3. **I2C mutex** - `I2CBus::take()`/`give()` protects ADS1115 + BH1750; ADC reads additionally serialized by an ADS1115-level mutex
4. **MQTT queue** - `MqttPublisher` runs dedicated task with `xQueue` for non-blocking publishes
5. **Change-threshold + heartbeat** - Publishes on value change (≥ threshold) OR every 60s
6. **Config via MQTT** - System config (day, phase, TDS/pH targets) received on retained topics; validity flag only true after all 3 config messages arrive
7. **Relay command queue** - Relay state mutated only by its own task via queue; MQTT callback just enqueues
8. **Network startup gate** - WiFiManager connects first (including its AP portal fallback), then OTA starts and only afterward are sensor, relay, and output tasks created; those tasks wait again if Wi-Fi disconnects

### MQTT Topics

| Direction | Topic | Payload |
|-----------|-------|---------|
| Pub | `uji-prototype/sensor/tds` | `{"tds_ppm":123.4}` or `{"tds_ppm":null}` |
| Pub | `uji-prototype/sensor/temperature` | `{"temperature_c":25.67}` |
| Pub | `uji-prototype/sensor/distance` | `{"distance_cm":12.3}` |
| Pub | `uji-prototype/sensor/light` | `{"light_lux":450.0}` |
| Pub | `uji-prototype/sensor/turbidity` | `{"turbidity_ntu":12.5}` |
| Pub | `uji-prototype/sensor/ph` | `{"ph":6.25}` |
| Pub | `uji-prototype/relay/status` | `{"relay1":true,...,"state_mask":0x0F}` |
| Sub | `uji-prototype/relay/command` | `{"relay":3,"state":true}` |
| Sub | `uji-prototype/system/config` | `{"hari_ke":12,"fase":"pertumbuhan",...}` |
| Sub | `uji-prototype/config/tds` | `{"tds_min":500,"tds_max":600,"target_tds":550}` |
| Sub | `uji-prototype/config/ph` | `{"ph_min":5.5,"ph_max":6.5,"target_ph":6.0}` |
| Sub | `uji-prototype/sensor/request` | `{"sensor":"tds"}` triggers immediate publish |

### Calibration Notes

- **TDS**: ADS1115 ch0, temperature-compensated, median-of-9 + average-of-10 filtering (see `test/TDS_Cal.cpp`). Piecewise-linear calibration in `TdsSensor.cpp` `CAL[]`: 359/500/718/1000 ppm at compensated voltages ~0.97/1.32/1.71/2.27 V; below ~0.0065V treated as dry
- **pH**: ADS1115 ch2, temperature-compensated via Kelvin ratio to calibration temp 26.62°C. 3-point linear interpolation: pH 9.17/6.86/4.01 at 1.165/1.546/2.040 V
- **Turbidity**: ADS1115 ch1, piecewise-linear 3-point calibration (0.43/18.2/186 NTU at ~2.81/2.41/1.51 V) - see `calibration/TurbidityCalibrationData.md` (coffee point provisional)
- **Distance**: Linear regression correction (scale 1.0337, offset +0.99 cm), valid range 20-600 cm, median-of-7 window, stale after 5s without valid echo

All ADC sensors share `Ads1115Manager::readFilteredVoltage()` — recalibrate by editing the module's `CAL[]`, not the manager.

### WiFi Provisioning

WiFiManager AP fallback: SSID `TDS-MQTT-Setup`; password lokal disimpan di `include/secrets.h` (portal timeout 180s). `include/secrets.h` tidak boleh di-commit.

OTA uses hostname `hidroponik-esp32.local`, port 3232, and a password shared between `include/secrets.h` and the ignored `platformio_override.ini`.

### Node-RED Automation

`node-red/flow_hidroponik_modular.json` (import into Node-RED; broker `broker.emqx.io:1883`):
- Inject node runs every 60s; `function-config` computes `hari_ke`/fase from `global.get('tanggal_tanam')` (set this global var once, format YYYY-MM-DD) and publishes all 3 retained configs
- Growth phases: hari 1-6 penyemaian, 7-11 awal_nutrisi (TDS 285-300), 12-26 pertumbuhan (500-600, target 550), 27+ menjelang_panen (775-800, target 800); pH always 5.5-6.5 target 6.0
- Sensor hub routes all `uji-prototype/sensor/+` to control functions
- **TDS control** (Relay 1+2): Doses 2s when below target, waits 60s settle, then requests fresh TDS reading
- **pH control** (Relay 3=pH Up when low, 4=pH Down when high): Doses 2s when outside range, waits 60s
- Commands sent to `uji-prototype/relay/command`

### Build Config (`platformio.ini`)

- Board: `esp32-s3-devkitc-1` with 16MB flash, QIO OPI PSRAM
- Upload: COM7, esptool; monitor 115200 baud
- OTA upload: use environment `esp32-s3-devkitc-1-n16r8-ota` after the first USB upload
- Excluded from build: `PhTemperatureCalibration.cpp`, `TurbidityCalibration.cpp`
- Deps: Adafruit ADS1X15, OneWire, DallasTemperature, WiFiManager, PubSubClient

### Conventions

- User-facing strings (Serial logs, comments) are in Indonesian; identifiers in English — keep it that way
- MQTT payloads are hand-built with `snprintf`, parsed with naive string search (`readNumber`/`readText`) — keep field names lowercase snake_case and no spaces
