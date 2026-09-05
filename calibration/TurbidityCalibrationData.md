# Catatan Kalibrasi Turbidity

Status: **sementara, belum diterapkan ke firmware**

Rangkaian saat pengukuran:

- Sensor turbidity masuk ADS1115 kanal A1.
- Voltage divider terpasang dan harus dipertahankan tanpa perubahan.
- Seluruh GND tersambung bersama.

## Sampel referensi laboratorium

| Sampel | Referensi | Tegangan sesi terbaru |
|---|---:|---:|
| Air biasa | 0,43 NTU | 2,9918 V |
| Air teh | 18,2 NTU | 2,9738 V |
| Air kopi | 186 NTU | 1,4117 V (sementara) |

## Statistik air biasa

- Data stabil: 2,9912; 2,9936; 2,9918; 2,9910; 2,9917; 2,9937 V.
- Median: 2,9918 V.

## Statistik air teh

- Data stabil: 2,9793; 2,9723; 2,9699; 2,9696; 2,9741; 2,9738; 2,9755 V.
- Median: 2,9738 V.

## Statistik air kopi

Prosedur sementara:

1. Sampel dihomogenkan.
2. Dua pembacaan pertama dibuang.
3. Empat pembacaan berikutnya digunakan.

Data evaluasi: 1,3765; 1,3961; 1,4273; 1,4527 V.

- Rata-rata: 1,4132 V.
- Median: 1,4117 V.
- Setelah jendela selesai, tegangan terus naik ke 1,4654; 1,5022; dan
  1,5268 V akibat pengendapan.

Titik kopi belum final karena belum reproducible. Saat pengujian dilanjutkan,
gunakan pengadukan atau sirkulasi konstan dan ambil 20-30 data stabil sebelum
memperbarui `src/Turbidity/TurbiditySensor.cpp`.

## Kalibrasi yang masih aktif di firmware

```cpp
{2.81200f,   0.43f},
{2.41055f,  18.20f},
{1.51295f, 186.00f}
```

Jangan mengganti titik aktif menggunakan data sesi terbaru sebelum titik kopi
divalidasi ulang.
