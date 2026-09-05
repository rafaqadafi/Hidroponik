# Sistem Monitoring Hidroponik ESP32-S3

Project prototipe monitoring dan otomasi hidroponik menggunakan ESP32-S3, Arduino, PlatformIO, MQTT, dan Node-RED. ESP32 membaca sensor serta menerima perintah relay, sedangkan Node-RED mengelola target nutrisi dan pH berdasarkan fase pertumbuhan tanaman.

## Fitur

- Monitoring TDS, pH, suhu air, kekeruhan, jarak permukaan air, dan intensitas cahaya.
- Pengiriman data MQTT saat nilai berubah melewati threshold atau melalui heartbeat 60 detik.
- Konfigurasi WiFi melalui portal WiFiManager.
- Kontrol 8 channel relay melalui 74HC595 dan antrean perintah MQTT.
- Task FreeRTOS dengan mutex untuk berbagi data dan akses I2C.
- Konfigurasi pin dan parameter operasional terpusat.
- Otomasi dosing nutrisi dan pH melalui flow Node-RED.

## Hardware dan pemetaan pin

Board dikonfigurasi sebagai ESP32-S3 DevKitC-1 N16R8 dengan 16 MB flash dan 8 MB PSRAM.

| Perangkat / sinyal | GPIO ESP32-S3 |
| --- | --- |
| I2C SDA: ADS1115 dan BH1750 | 8 |
| I2C SCL: ADS1115 dan BH1750 | 9 |
| Data DS18B20 | 4 |
| Trigger ultrasonik | 12 |
| Echo ultrasonik | 14 |
| 74HC595 SER | 10 |
| 74HC595 RCLK | 18 |
| 74HC595 SRCLK | 11 |

| Input analog | Channel ADS1115 |
| --- | --- |
| TDS | A0 |
| Turbidity | A1 |
| pH | A2 |

Alamat I2C ADS1115 adalah `0x48`, sedangkan BH1750 adalah `0x23`. Relay dikonfigurasi active-low.

Driver ultrasonik menggunakan pulsa Trigger/Echo. Untuk modul SR04M, pastikan varian dan mode modul sesuai; lihat label Trig/RX dan Echo/TX pada dokumentasi modul. Jalur output sensor berlevel 5 V perlu penyesuaian level sebelum masuk GPIO ESP32. Tabel di atas menunjukkan pemetaan firmware, bukan skema rangkaian lengkap.

## Persiapan

1. Instal VS Code dan ekstensi PlatformIO IDE.
2. Clone repository ini, lalu buka folder project di VS Code.
3. Salin template credential melalui terminal PowerShell dari folder project:

   ```powershell
   Copy-Item include/secrets.example.h include/secrets.h
   ```

4. Isi `HYDRO_WIFI_AP_PASSWORD` dan `HYDRO_OTA_PASSWORD` pada `include/secrets.h` dengan password lokal. Gunakan password AP minimal 8 karakter. File ini diabaikan Git; jangan memasukkan password asli ke template atau dokumentasi.
5. Sambungkan ESP32 melalui USB. Sesuaikan `upload_port` dan `monitor_port` di [platformio.ini](platformio.ini) jika port perangkat bukan `COM7`.

PlatformIO memasang dependency yang tercantum pada `lib_deps` saat build. Jalankan perintah berikut melalui terminal PlatformIO.

## Build dan upload USB

```powershell
pio run
pio run -t upload
pio device monitor -p COM7 -b 115200
```

Tutup Serial Monitor sebelum upload jika port sedang digunakan. Konfigurasi PlatformIO saat ini hanya menyediakan upload USB menggunakan `esptool`.

Modul layanan OTA masih ada dalam firmware dan menggunakan password dari `secrets.h`, tetapi environment upload OTA sudah dihapus. `platformio_override.ini` tidak diperlukan untuk build atau upload USB saat ini.

## Menghubungkan WiFi

1. Saat menyala, ESP32 mencoba kredensial WiFi tersimpan.
2. Jika gagal, sambungkan komputer atau ponsel ke AP `TDS-MQTT-Setup` menggunakan password AP yang diisi di `secrets.h`.
3. Buka portal WiFiManager, pilih jaringan WiFi, lalu simpan credential.
4. Periksa Serial Monitor untuk melihat alamat IP dan status koneksi.

Portal memiliki timeout 180 detik dan percobaan koneksi diulang jika belum berhasil. Saat boot, task sensor, relay, dan output baru dibuat setelah WiFi tersambung dan layanan OTA diinisialisasi. Koneksi broker MQTT bukan syarat pelepasan task tersebut.

Jika task jaringan mendeteksi WiFi putus, `NetworkGate` ditutup. Task aplikasi menunggu pada titik tunggu berikutnya sampai koneksi tersedia kembali. Ini tidak otomatis mematikan relay; relay mempertahankan keadaan terakhirnya.

ADS1115 yang tidak ditemukan tidak menghentikan startup jaringan. Sensor yang bergantung pada ADS1115 tidak dijalankan, sementara modul lainnya tetap dapat berjalan setelah gerbang jaringan terbuka.

## MQTT dan Node-RED

Broker yang dikonfigurasi adalah `broker.emqx.io:1883`. Topic menggunakan prefix `uji-prototype`.

| Fungsi | Topic |
| --- | --- |
| Data sensor | `uji-prototype/sensor/tds`, `temperature`, `distance`, `light`, `turbidity`, `ph` di bawah prefix `uji-prototype/sensor/` |
| Perintah relay | `uji-prototype/relay/command` |
| Status relay | `uji-prototype/relay/status` |
| Konfigurasi sistem | `uji-prototype/system/config` |
| Target TDS | `uji-prototype/config/tds` |
| Target pH | `uji-prototype/config/ph` |
| Permintaan pembacaan | `uji-prototype/sensor/request` |

Untuk menjalankan otomasi:

1. Import [flow_hidroponik_modular.json](node-red/flow_hidroponik_modular.json) ke Node-RED.
2. Periksa konfigurasi broker dan topic agar sesuai firmware.
3. Atur global context `tanggal_tanam` dengan format `YYYY-MM-DD`.
4. Deploy flow dan pastikan konfigurasi sistem, TDS, serta pH diterima melalui topic retained sebelum menggunakan otomasi dosing.

Contoh payload perintah relay:

```json
{"relay":1,"state":true}
```

Broker publik dan prefix topic bersama tidak memberikan isolasi perangkat. Untuk penggunaan nyata, sesuaikan broker, akses, dan topic dengan instalasi sendiri.

## Konfigurasi dan kalibrasi

- [src/Config/config.cpp](src/Config/config.cpp): pin, alamat I2C, channel ADC, interval sampling, threshold, dan parameter jaringan.
- [src/Config/config.h](src/Config/config.h): deklarasi konfigurasi.
- Kalibrasi TDS, pH, dan turbidity berada dalam `*Sensor.cpp` masing-masing.
- Kalibrasi ultrasonik tetap berada di [UltrasonicSensor.cpp](src/Ultrasonic/UltrasonicSensor.cpp). Rentang valid firmware saat ini 20–600 cm; ini bukan jaminan spesifikasi semua varian sensor.

Setelah mengubah konfigurasi atau kalibrasi, build dan upload ulang firmware.

## Struktur project

```text
src/
  Config/          Konfigurasi operasional
  Network/         Gerbang konektivitas task
  MQTT/            Koneksi broker dan antrean publish
  OTA/             Layanan ArduinoOTA
  I2C/             Mutex bus I2C
  ADS1115/         Pembacaan ADC terfilter
  TDS/             Sensor TDS
  PH/              Sensor pH
  Turbidity/       Sensor kekeruhan
  Temperature/     Sensor suhu
  Ultrasonic/      Sensor jarak
  LightSensor/     Sensor cahaya
  Relay/           Kontrol relay
  main.cpp         Startup dan output data
include/           Header dan template credential
node-red/          Flow otomasi
calibration/       Catatan kalibrasi
test/              Sketch kalibrasi mandiri
```

Belum tersedia test runner otomatis. `test/TDS_Cal.cpp` merupakan sketch mandiri. Sketch `PhTemperatureCalibration.cpp` dan `TurbidityCalibration.cpp` dikecualikan dari build utama.
