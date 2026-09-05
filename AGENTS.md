# AGENTS.md

ESP32-S3 hydroponic monitor berbasis PlatformIO + Arduino. Alur utama: sensor → MQTT → Node-RED automation.

## Commands

```bash
# Build firmware USB/default
pio run -e esp32-s3-devkitc-1-n16r8

# Build firmware OTA
pio run -e esp32-s3-devkitc-1-n16r8-ota

# Upload pertama atau recovery melalui USB COM7
pio run -e esp32-s3-devkitc-1-n16r8 -t upload

# Upload berikutnya melalui OTA; ESP32 harus sudah terhubung ke jaringan
pio run -e esp32-s3-devkitc-1-n16r8-ota -t upload

# Monitor serial
pio device monitor -p COM7 -b 115200

# Bersihkan hasil build
pio run -t clean
```

Tidak ada test runner. `test/TDS_Cal.cpp` adalah sketch kalibrasi mandiri dan tidak masuk main build.

## Hardware

- Board: ESP32-S3 DevKitC-1, 16 MB flash + 8 MB Octal PSRAM.
- ADS1115 ADC pada I2C `0x48`.
- BH1750 light sensor pada I2C `0x23`.
- DS18B20 temperature sensor.
- HC-SR04 ultrasonic distance sensor.
- 74HC595 relay driver untuk 8 channel relay.

Pin dan alamat perangkat berada di `src/Config/config.cpp`, bukan tersebar di setiap modul.

## Firmware architecture

Firmware menggunakan task FreeRTOS dengan mutex dan queue:

- `src/Config/` berisi konfigurasi terpusat: pin, alamat I2C, channel ADC, interval sampling, valid range, threshold output, MQTT, dan OTA.
- `src/Network/NetworkGate.*` berisi event gate bersama untuk status Wi-Fi + OTA siap.
- `src/MQTT/MqttPublisher.cpp` memiliki task jaringan yang menjalankan WiFiManager, PubSubClient, queue publish, dan pemulihan koneksi.
- `src/OTA/OTAManager.cpp` mengelola ArduinoOTA di dalam task jaringan; OTA bukan task terpisah.
- `src/ADS1115/` mengelola ADS1115 dan pembacaan tegangan terfilter.
- Setiap sensor memiliki namespace, `begin()`, status/getter bermutex, dan task sampling sendiri.
- `main.cpp` memiliki `outputTask` yang membaca sensor setiap 1 detik dan publish saat melewati threshold atau heartbeat 60 detik.
- `src/Relay/Relay.cpp` memproses command melalui queue; perubahan GPIO relay hanya dilakukan oleh task relay.
- `src/I2C/I2CBus.*` melindungi ADS1115 dan BH1750 dengan `I2CBus::take()`/`give()`.

## Startup and task priority

Urutan startup harus dipertahankan:

1. Inisialisasi Serial, Wire, dan mutex I2C.
2. Membuat `NetworkGate` dan task `MqttPublisher`.
3. WiFiManager mencoba kredensial tersimpan. Jika gagal, WiFiManager membuat AP konfigurasi sampai portal timeout 180 detik; proses diulang sampai berhasil.
4. Setelah Wi-Fi tersambung, `OTAManager` menjalankan ArduinoOTA.
5. `NetworkGate` dibuka setelah Wi-Fi + OTA siap.
6. Baru kemudian ADS1115, sensor, relay, dan `outputTask` dibuat.
7. MQTT broker dicoba setelah jaringan siap; koneksi MQTT bukan syarat untuk membuka task sensor.

Task jaringan adalah satu-satunya task yang boleh aktif untuk bootstrap koneksi. Jika Wi-Fi terputus, gate ditutup dan task sensor, relay, serta output menunggu `NetworkGate::waitUntilConnected()` sampai WiFiManager berhasil menyambung lagi.

Prioritas task saat ini: sensor TDS/pH/turbidity/suhu/ultrasonik dan relay = 2; task MQTT, BH1750, dan output = 1.

Kegagalan ADS1115 tidak boleh menghentikan startup atau memblokir Wi-Fi/OTA. TDS, pH, dan turbidity boleh tidak dibuat saat ADS tidak terdeteksi, sedangkan task yang tidak bergantung pada ADS tetap berjalan setelah jaringan siap.

## Configuration rules

Ubah konfigurasi operasional di `src/Config/config.cpp`, deklarasinya ada di `src/Config/config.h`. Kelompok konfigurasi:

- `Config::Pins`: GPIO I2C, DS18B20, HC-SR04, dan 74HC595.
- `Config::I2C`: alamat ADS1115 dan BH1750.
- `Config::Adc`: median, average, delay sample, dan faktor LSB ADS1115.
- `Config::Tds`, `Config::Ph`, `Config::Turbidity`, `Config::Temperature`: channel, interval, dan valid range.
- `Config::Ultrasonic`: parameter trigger, timeout echo, interval, stale timeout, dan median window.
- `Config::Light`: command BH1750, interval, dan pembagi lux.
- `Config::Relay`: active-low, jumlah channel, mask, dan panjang queue.
- `Config::Output`: threshold perubahan, refresh serial, interval task, dan heartbeat MQTT.
- `Config::Mqtt`: broker, topic, WiFiManager AP, ukuran buffer, timeout, dan interval retry.
- `Config::Ota`: hostname `hidroponik-esp32` dan password OTA.

Kalibrasi tetap berada di modul sensor masing-masing sebagai array `CAL[]` atau konstanta kalibrasi. Jangan memindahkan kalibrasi ke `Config` hanya untuk mengubah pin/parameter operasional.

## Calibration

- TDS: `src/TDS/TdsSensor.cpp`, temperature-compensated dan dry threshold sekitar `0.0065 V`.
- pH: `src/PH/PhSensor.cpp`, temperature-compensated dengan interpolasi 3 titik.
- Turbidity: `src/Turbidity/TurbiditySensor.cpp`, interpolasi piecewise 3 titik.
- Ultrasonic: `src/Ultrasonic/UltrasonicSensor.cpp`; kalibrasi dan valid range yang sudah ada harus dibiarkan seperti semula. Saat ini valid range modul adalah 20–600 cm, median window 7, dan stale setelah 5 detik tanpa echo valid.

`PhTemperatureCalibration.cpp` dan `TurbidityCalibration.cpp` adalah sketch kalibrasi terpisah dan dikecualikan dari main build melalui `platformio.ini`.

## WiFi, OTA, and MQTT

- WiFiManager AP fallback: SSID `TDS-MQTT-Setup`, password `tdsmqtt123`, portal timeout 180 detik.
- OTA hostname: `hidroponik-esp32.local`, port `3232`.
- Password lokal disimpan di `include/secrets.h` dan `platformio_override.ini`; kedua file tersebut di-ignore Git.
- `Config::Ota::PASSWORD` harus sama dengan nilai `--auth` pada `platformio_override.ini`.
- Upload OTA harus dilakukan dari komputer pada jaringan yang sama. Jika hostname `.local` tidak ditemukan, gunakan IP ESP32 sebagai `upload_port`.
- MQTT broker: `broker.emqx.io:1883`.
- Konfigurasi retained MQTT wajib diterima sebelum Node-RED memulai dosing.
- MQTT JSON dibuat dengan `snprintf` dan parser sederhana `readNumber`/`readText`; pertahankan field lowercase_snake_case.

## Node-RED

`node-red/flow_hidroponik_modular.json` berisi logika fase pertumbuhan dan dosing TDS/pH. Flow membaca `global.get('tanggal_tanam')` dengan format `YYYY-MM-DD`, menerbitkan target fase melalui topic retained, lalu mengirim command relay melalui MQTT.

## Common gotchas

- Jangan menaruh inisialisasi atau loop sensor sebelum `NetworkGate` terbuka.
- Jangan membuat ADS1115 menjadi syarat Wi-Fi atau OTA.
- Jangan commit password, token, atau credential. Gunakan `include/secrets.h` dan `platformio_override.ini` lokal.
- Semua perangkat I2C baru wajib memakai `I2CBus::take()`/`give()`.
- Kontrol relay harus melalui `Relay::setRelay()`, `Relay::allOn()`, atau `Relay::allOff()`; jangan menulis GPIO relay langsung dari callback MQTT.
- `UltrasonicSensor::getDistanceCm()` dapat mengembalikan `false` setelah echo tidak valid/stale; periksa nilai return sebelum publish atau memakai jarak.
- TDS publish `null` saat tegangan berada di bawah dry threshold sekitar `0.0065 V`.
- String Serial dan komentar menggunakan Bahasa Indonesia; identifier kode menggunakan Bahasa Inggris.
