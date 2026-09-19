# Sistem Monitoring Hidroponik ESP32-S3

Project prototipe monitoring dan otomasi hidroponik menggunakan ESP32-S3, Arduino, PlatformIO, dan MQTT. Pada branch uji coba ini ESP32 membaca sensor sekaligus menjalankan logic threshold lokal; MQTT hanya mengirim telemetry dan status aktuator untuk dipantau komputer edge.

## Fitur

- Monitoring TDS, pH, suhu air, kekeruhan, jarak permukaan air, intensitas cahaya,
  empat float switch, serta debit dan volume air.
- Pengiriman data MQTT saat nilai berubah melewati threshold atau melalui heartbeat 60 detik.
- Konfigurasi WiFi melalui portal WiFiManager.
- Kontrol 8 channel relay melalui 74HC595 dari modul `src/Logic`.
- Mapping aktuator bernama: `ph_up_pump`, `ph_down_pump`, `nutrient_pump`,
  `water_pump`, `solenoid_valve`, `grow_light`, dan `aerator`.
- MQTT publish-only pada branch uji coba ini. ESP32 mengirim sensor, tegangan
  turbidity, status logic, dan kondisi aktuator; tidak menerima command relay.
- Failsafe lokal tetap menjaga float switch, safe-off, durasi internal, dan
  hardware watchdog.
- TFT ILI9488 memakai SPI 20 MHz dan readback SDO/MISO pada GPIO15; re-inisialisasi
  controller hanya dilakukan saat pemeriksaan status gagal tanpa me-reset ESP32.
- Tampilan TFT memiliki dua halaman bergaya telemetry: halaman angka untuk seluruh
  sensor termasuk flow meter, serta halaman grafik historis pH dan cahaya. Halaman
  berganti otomatis setiap 10 detik dan menyimpan 48 sampel terakhir.
- Task FreeRTOS dengan mutex untuk berbagi data dan akses I2C.
- Konfigurasi pin dan parameter operasional terpusat.
- Logic lokal dibuat sementara agar mudah dipindahkan ke komputer edge pada tahap berikutnya.

## Hardware dan pemetaan pin

Board dikonfigurasi sebagai ESP32-S3 DevKitC-1 N16R8 dengan 16 MB flash dan 8 MB PSRAM.

| Perangkat / sinyal | GPIO ESP32-S3 |
| --- | --- |
| I2C SDA: ADS1115 dan BH1750 | 8 |
| I2C SCL: ADS1115 dan BH1750 | 9 |
| Data DS18B20 | 6 |
| Trigger ultrasonik | 12 |
| Echo ultrasonik | 14 |
| Buzzer aktif melalui basis transistor | 7 |
| Float switch pH-Up | 40 |
| Float switch Nutrisi A | 41 |
| Float switch Nutrisi B | 42 |
| Float switch pH-Down | 39 |
| Pulsa flow meter YF-S201 | 1 |
| TFT ILI9488 SCLK | 16 |
| TFT ILI9488 MOSI/SDI | 13 |
| TFT ILI9488 MISO/SDO | 15 |
| TFT ILI9488 DC/RS | 5 |
| TFT ILI9488 CS | 4 |
| TFT ILI9488 RST | 2 |
| 74HC595 SER | 10 |
| 74HC595 RCLK | 18 |
| 74HC595 SRCLK | 11 |

| Input analog | Channel ADS1115 |
| --- | --- |
| TDS | A0 |
| Turbidity | A1 |
| pH | A2 |

Alamat I2C ADS1115 adalah `0x48`, sedangkan BH1750 adalah `0x23`. Relay dikonfigurasi active-low.

Buzzer aktif dikendalikan melalui basis transistor pada GPIO 7. GPIO 33-37 dicadangkan
untuk Octal-PSRAM pada board N16R8 dan tidak digunakan sebagai GPIO eksternal. Buzzer menyala ketika
float switch pH-Up mendeteksi cairan habis. Alarm mengulang tiga bip pendek selama kondisi
cairan habis, lalu berhenti ketika level kembali normal.

Driver ultrasonik menggunakan pulsa Trigger/Echo. Untuk modul SR04M, pastikan varian dan mode modul sesuai; lihat label Trig/RX dan Echo/TX pada dokumentasi modul. Jalur output sensor berlevel 5 V perlu penyesuaian level sebelum masuk GPIO ESP32. Tabel di atas menunjukkan pemetaan firmware, bukan skema rangkaian lengkap.

Flow meter YF-S201 diberi supply 5 V. Jalur output pulsa melewati pembagi tegangan 10 kΩ
seri dan 22 kΩ ke GND sebelum masuk GPIO1. Perhitungan volume menggunakan sekitar 450 pulsa
per liter; volume dihitung sejak perangkat menyala dan belum disimpan permanen.

## Persiapan

1. Instal VS Code dan ekstensi PlatformIO IDE.
2. Clone repository ini, lalu buka folder project di VS Code.
3. Salin template credential melalui terminal PowerShell dari folder project:

   ```powershell
   Copy-Item include/secrets.example.h include/secrets.h
   ```

4. Isi `HYDRO_WIFI_AP_PASSWORD` pada `include/secrets.h` dengan password lokal. Gunakan password AP minimal 8 karakter. File ini diabaikan Git; jangan memasukkan password asli ke template atau dokumentasi.
5. Sambungkan ESP32 melalui USB. Sesuaikan `upload_port` dan `monitor_port` di [platformio.ini](platformio.ini) jika port perangkat bukan `COM7`.

PlatformIO memasang dependency yang tercantum pada `lib_deps` saat build. Jalankan perintah berikut melalui terminal PlatformIO.

## Build dan upload USB

```powershell
pio run
pio run -t upload
pio device monitor -p COM7 -b 115200
```

Tutup Serial Monitor sebelum upload jika port sedang digunakan. Konfigurasi PlatformIO saat ini hanya menyediakan upload USB menggunakan `esptool`.

Layanan OTA dinonaktifkan. Upload firmware dilakukan melalui USB menggunakan `esptool`.

## Menghubungkan WiFi

1. Saat menyala, ESP32 mencoba kredensial WiFi tersimpan.
2. Jika gagal, sambungkan komputer atau ponsel ke AP `TDS-MQTT-Setup` menggunakan password AP yang diisi di `secrets.h`.
3. Buka portal WiFiManager, pilih jaringan WiFi, lalu simpan credential.
4. Periksa Serial Monitor untuk melihat alamat IP dan status koneksi.

Portal memiliki timeout 180 detik dan percobaan koneksi diulang jika belum berhasil. Saat boot, task sensor, relay, dan output baru dibuat setelah WiFi tersambung. Koneksi broker MQTT bukan syarat pelepasan task tersebut.

Jika task jaringan mendeteksi WiFi putus, `NetworkGate` ditutup. Task aplikasi menunggu pada titik tunggu berikutnya sampai koneksi tersedia kembali. Ini tidak otomatis mematikan relay; relay mempertahankan keadaan terakhirnya.

ADS1115 yang tidak ditemukan tidak menghentikan startup jaringan. Sensor yang bergantung pada ADS1115 tidak dijalankan, sementara modul lainnya tetap dapat berjalan setelah gerbang jaringan terbuka.

## MQTT

Broker yang dikonfigurasi adalah `192.168.1.75:1883`.

| Fungsi | Topic |
| --- | --- |
| Telemetry sensor, logic, dan aktuator | `farming/ESP32-HYDROPONIC-01/hydroponic/data` |
| Control relay (disiapkan untuk migrasi edge) | `farming/ESP32-HYDROPONIC-01/hydroponic/control` |
| Heartbeat edge (disiapkan untuk migrasi edge) | `farming/ESP32-HYDROPONIC-01/hydroponic/heartbeat` |

Payload telemetry memiliki sensor dan status aktuator bernama dalam satu pesan. Contoh bentuknya:

```json
{
  "payload": [{
    "sensors": {
      "ph": 6.4,
      "tds": 500,
      "turbidity_voltage": 2.75,
      "turbidity_status": "cloudy"
    },
    "actuators": {
      "ph_up_pump": 0,
      "ph_down_pump": 0,
      "nutrient_pump": 1,
      "water_pump": 0,
      "solenoid_valve": 0,
      "grow_light": 1,
      "aerator": 1
    }
  }]
}
```

Turbidity mengirim `turbidity_voltage` dalam volt dan status `clear`, `cloudy`,
atau `dirty`. Status `dirty` tidak mematikan logic; tegangan tetap disertakan
agar kondisi sensor dapat diperiksa dari telemetry.

Pada tahap migrasi edge, komputer lokal dapat subscribe topic telemetry tersebut
dan kemudian mengambil alih decision rule. Topic control dan heartbeat tetap
dicadangkan untuk mode edge berikutnya, tetapi belum disubscribe firmware branch ini.

Broker publik dan prefix topic bersama tidak memberikan isolasi perangkat. Untuk penggunaan nyata, sesuaikan broker, akses, dan topic dengan instalasi sendiri.

## Konfigurasi dan kalibrasi

- [src/Config/config.cpp](src/Config/config.cpp): pin, alamat I2C, channel ADC, interval sampling, threshold, dan parameter jaringan.
- [src/Config/config.h](src/Config/config.h): deklarasi konfigurasi.
- Kalibrasi TDS dan pH berada dalam `*Sensor.cpp` masing-masing. Turbidity memakai threshold tegangan di `src/Config/config.cpp` tanpa konversi NTU.
- Kalibrasi ultrasonik tetap berada di [UltrasonicSensor.cpp](src/Ultrasonic/UltrasonicSensor.cpp). Rentang valid firmware saat ini 20–600 cm; ini bukan jaminan spesifikasi semua varian sensor.

Turbidity tidak dikonversi ke NTU karena pembacaan dipengaruhi cahaya sekitar dan posisi sensor. Firmware memakai hasil kalibrasi tegangan ADS1115 A1: referensi air jernih `Config::Turbidity::CLEAR_WATER_THRESHOLD_VOLTAGE = 2.9533 V`, sedangkan tegangan di bawah `Config::Turbidity::CLOUDY_WATER_REFERENCE_VOLTAGE = 2.6015 V` diklasifikasikan sebagai `AIR KOTOR`. Rentang di antara kedua threshold diklasifikasikan sebagai `AIR KERUH`. MQTT mengirim tegangan dan status tersebut.

Perhitungan flow menggunakan konstanta datasheet sekitar 450 pulsa per liter pada `Config::Flow::PULSES_PER_LITER`.

Setelah mengubah konfigurasi atau kalibrasi sensor, build dan upload ulang firmware.

TDS pada tegangan terkompensasi <= 0,0065 V menghasilkan 0 ppm. Payload `null` digunakan ketika pembacaan dinilai tidak valid oleh firmware, misalnya sensor TDS atau suhu belum siap.

## Dokumentasi lokal dan Git

`README.md` merupakan halaman dokumentasi GitHub. `AGENTS.md` diabaikan Git sebagai panduan kerja lokal. File credential `include/secrets.h` dan hasil build `.pio` juga diabaikan. Template credential tetap disertakan agar project dapat disiapkan pada komputer lain.

## Struktur project

```text
src/
  Config/          Konfigurasi operasional
  Network/         Gerbang konektivitas task
  MQTT/            Koneksi broker dan antrean publish
  I2C/             Mutex bus I2C
  ADS1115/         Pembacaan ADC terfilter
  TDS/             Sensor TDS
  PH/              Sensor pH
  Turbidity/       Sensor kekeruhan
  Temperature/     Sensor suhu
  Ultrasonic/      Sensor jarak
  LightSensor/     Sensor cahaya
  Relay/           Kontrol relay
  Logic/           FSM threshold lokal dan mapping aktuator sementara
  Failsafe/        Safe-off, float switch, dan watchdog
  main.cpp         Startup dan output data
include/           Header dan template credential
test/              Sketch kalibrasi mandiri
```

Belum tersedia test runner otomatis. `test/TDS_Cal.cpp` merupakan sketch mandiri. Sketch `PhTemperatureCalibration.cpp` dan `TurbidityCalibration.cpp` dikecualikan dari build utama.
