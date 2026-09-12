[CmdletBinding()]
param(
    [string]$VaultPath = 'D:\MemoryAgent\persistent_memory',
    [switch]$Watch,
    [switch]$InstallWatcher,
    [switch]$UninstallWatcher,
    [int]$PollSeconds = 3
)

$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$vaultProjectPath = Join-Path $VaultPath 'Nad Hydroponic Monitor'
$watcherTaskName = 'Nad Obsidian Sync'
$scriptPath = (Resolve-Path $PSCommandPath).Path

function Get-ProjectSourceFiles {
    $files = New-Object 'System.Collections.Generic.List[string]'
    foreach ($relativePath in @('README.md', 'AGENTS.md', 'platformio.ini')) {
        $fullPath = Join-Path $projectRoot $relativePath
        if (Test-Path -LiteralPath $fullPath -PathType Leaf) {
            $files.Add((Resolve-Path -LiteralPath $fullPath).Path)
        }
    }
    foreach ($relativeDirectory in @('Notes', 'src', 'hardware', 'node-red')) {
        $directoryPath = Join-Path $projectRoot $relativeDirectory
        if (Test-Path -LiteralPath $directoryPath -PathType Container) {
            Get-ChildItem -LiteralPath $directoryPath -File -Recurse -Force |
                Where-Object { $_.FullName -notmatch '\\.pio([\\/]|$)' } |
                ForEach-Object { $files.Add($_.FullName) }
        }
    }
    return $files | Sort-Object -Unique
}

function Get-FileState {
    $state = [ordered]@{}
    foreach ($filePath in Get-ProjectSourceFiles) {
        $item = Get-Item -LiteralPath $filePath
        $state[$filePath] = '{0}|{1}|{2}' -f $item.Length, $item.LastWriteTimeUtc.Ticks, $item.Attributes
    }
    return $state
}

function Get-ConfigValue {
    param([string]$Namespace, [string]$Name)

    $configPath = Join-Path $projectRoot 'src\Config\config.cpp'
    if (-not (Test-Path -LiteralPath $configPath)) { return 'tidak ditemukan' }

    $configText = Get-Content -LiteralPath $configPath -Raw
    $namespacePattern = 'namespace\s+' + [regex]::Escape($Namespace) + '\s*\{(?<body>.*?)\r?\n\}'
    $namespaceMatch = [regex]::Match($configText, $namespacePattern, [System.Text.RegularExpressions.RegexOptions]::Singleline)
    if (-not $namespaceMatch.Success) { return 'tidak ditemukan' }

    $valuePattern = 'const\s+[^;\r\n]+?\s+' + [regex]::Escape($Name) + '(?:\[\])?\s*=\s*(?<value>[^;]+);'
    $valueMatch = [regex]::Match($namespaceMatch.Groups['body'].Value, $valuePattern)
    if (-not $valueMatch.Success) { return 'tidak ditemukan' }

    return $valueMatch.Groups['value'].Value.Trim()
}

function Write-Utf8File {
    param([string]$Path, [string]$Content)
    Set-Content -LiteralPath $Path -Value $Content -Encoding utf8
}

function Sync-Obsidian {
    New-Item -ItemType Directory -Path $vaultProjectPath -Force | Out-Null
    $attachmentPath = Join-Path $vaultProjectPath 'Lampiran'
    New-Item -ItemType Directory -Path $attachmentPath -Force | Out-Null
    $syncTime = Get-Date

    $hubNote = @"
---
title: Nad Hydroponic Monitor - Project Hub
tags:
  - hidroponik
  - esp32-s3
  - platformio
  - mqtt
type: project-hub
source_repo: '$projectRoot'
last_sync: "$($syncTime.ToString('yyyy-MM-dd HH:mm:ss K'))"
---

# Nad Hydroponic Monitor

Catatan pusat proyek monitoring dan otomasi hidroponik berbasis ESP32-S3. Nilai operasional di bawah dibaca dari src/Config/config.cpp setiap kali sinkronisasi.

## Status sinkronisasi

- Sinkron terakhir: $($syncTime.ToString('yyyy-MM-dd HH:mm:ss K'))
- Sumber kode: $projectRoot
- Watcher: task Windows $watcherTaskName
- Dokumen sumber: [[01 - README (sinkron)]], [[02 - Aturan Lokal (sinkron)]], [[03 - Catatan Proyek (sinkron)]]
- Audit hardware: [[04 - Audit Pin GPIO]] dan [[Lampiran/audit_pin_gpio.xlsx]]

## Hardware dan pin firmware

| Fungsi | Nilai saat sinkronisasi |
|---|---|
| I2C SDA | GPIO $(Get-ConfigValue 'Pins' 'I2C_SDA') |
| I2C SCL | GPIO $(Get-ConfigValue 'Pins' 'I2C_SCL') |
| DS18B20 | GPIO $(Get-ConfigValue 'Pins' 'DS18B20') |
| Ultrasonic trigger | GPIO $(Get-ConfigValue 'Pins' 'ULTRASONIC_TRIGGER') |
| Ultrasonic echo | GPIO $(Get-ConfigValue 'Pins' 'ULTRASONIC_ECHO') |
| 74HC595 SER | GPIO $(Get-ConfigValue 'Pins' 'RELAY_SER') |
| 74HC595 RCLK | GPIO $(Get-ConfigValue 'Pins' 'RELAY_RCLK') |
| 74HC595 SRCLK | GPIO $(Get-ConfigValue 'Pins' 'RELAY_SRCLK') |
| LCD SCLK | GPIO $(Get-ConfigValue 'Pins' 'LCD_SCLK') |
| LCD MOSI | GPIO $(Get-ConfigValue 'Pins' 'LCD_MOSI') |
| LCD DC | GPIO $(Get-ConfigValue 'Pins' 'LCD_DC') |
| LCD CS | GPIO $(Get-ConfigValue 'Pins' 'LCD_CS') |
| LCD RST | GPIO $(Get-ConfigValue 'Pins' 'LCD_RST') |
| ADS1115 | $(Get-ConfigValue 'I2C' 'ADS1115_ADDRESS') |
| BH1750 | $(Get-ConfigValue 'I2C' 'BH1750_ADDRESS') |
| TDS channel | A$(Get-ConfigValue 'Tds' 'ADC_CHANNEL') |
| Turbidity channel | A$(Get-ConfigValue 'Turbidity' 'ADC_CHANNEL') |
| pH channel | A$(Get-ConfigValue 'Ph' 'ADC_CHANNEL') |

## Parameter penting

- Ultrasonik: trigger $(Get-ConfigValue 'Ultrasonic' 'TRIGGER_PULSE_US') us, timeout echo $(Get-ConfigValue 'Ultrasonic' 'ECHO_TIMEOUT_US') us, interval $(Get-ConfigValue 'Ultrasonic' 'SAMPLE_INTERVAL_MS') ms, stale $(Get-ConfigValue 'Ultrasonic' 'STALE_TIMEOUT_MS') ms, median window $(Get-ConfigValue 'Ultrasonic' 'MEDIAN_WINDOW').
- MQTT heartbeat: $(Get-ConfigValue 'Output' 'MQTT_HEARTBEAT_MS') ms.
- Relay: active-low $(Get-ConfigValue 'Relay' 'ACTIVE_LOW'), channel $(Get-ConfigValue 'Relay' 'CHANNEL_COUNT').
- Broker: $(Get-ConfigValue 'Mqtt' 'HOST'):$(Get-ConfigValue 'Mqtt' 'PORT').

## Kalibrasi yang dipertahankan

- TDS: titik CAL 359, 500, 718, dan 1000 ppm; kompensasi suhu aktif; dry threshold sekitar 0,0065 V.
- pH: interpolasi 3 titik sekitar pH 9,17 / 6,86 / 4,01.
- Turbidity: interpolasi piecewise 3 titik sekitar 0,43 / 18,2 / 186 NTU. Tegangan kalibrasi dibaca setelah pembagi tegangan rangkaian utama; firmware tidak membagi rasio lagi.
- Ultrasonik: regresi skala 1,0337 dan offset +0,99 cm; rentang valid 20 sampai 600 cm; median window 7; stale setelah 5 detik tanpa echo valid.

## Keputusan hardware penting

- Rangkaian pembagi ultrasonik 5 V ke GPIO harus memakai 22 k-ohm dari ECHO/TX ke node GPIO dan 10 k-ohm dari node ke GND.
- DQ DS18B20 bersifat open-drain. Pada skematik, R1 4,7 k-ohm terlihat menarik DQ/GPIO6 ke 5 V; bila tidak ada level shifter, ukur DQ saat idle sebelum menyimpulkan aman untuk ESP32-S3.
- GPIO12 tercatat dipakai firmware sebagai trigger ultrasonik dan juga muncul pada jalur analog Flow di skematik; periksa benturan fisik sebelum merakit.
- Audit lengkap pin dan benturan berada di workbook lokal pada lampiran.

## Alur firmware

1. Serial, I2C, mutex, dan task jaringan diinisialisasi.
2. WiFiManager menyambungkan Wi-Fi atau membuka AP konfigurasi.
3. NetworkGate dibuka setelah Wi-Fi tersambung.
4. Sensor, relay, dan output task dibuat; MQTT bukan syarat untuk memulai task sensor.
5. Saat Wi-Fi terputus, task menunggu gate berikutnya dan relay mempertahankan keadaan terakhir.

## Aturan perubahan

- Ubah pin dan parameter operasional hanya di src/Config/config.cpp.
- Pertahankan kalibrasi di modul sensor masing-masing.
- Jangan memasukkan include/secrets.h, password, atau token ke catatan yang dibagikan.
- Setelah perubahan firmware atau hardware, watcher akan memperbarui catatan ini dan snapshot sumber secara otomatis.

## Perintah manual

    powershell -ExecutionPolicy Bypass -File "$scriptPath"
    powershell -ExecutionPolicy Bypass -File "$scriptPath" -Watch
"@
    Write-Utf8File -Path (Join-Path $vaultProjectPath '00 - Project Hub.md') -Content $hubNote

    $readmeText = Get-Content -LiteralPath (Join-Path $projectRoot 'README.md') -Raw
    Write-Utf8File -Path (Join-Path $vaultProjectPath '01 - README (sinkron).md') -Content @"
---
title: Nad - README sinkron
source: '$projectRoot\README.md'
last_sync: "$($syncTime.ToString('yyyy-MM-dd HH:mm:ss K'))"
---

$readmeText
"@

    $agentsText = Get-Content -LiteralPath (Join-Path $projectRoot 'AGENTS.md') -Raw
    Write-Utf8File -Path (Join-Path $vaultProjectPath '02 - Aturan Lokal (sinkron).md') -Content @"
---
title: Nad - Aturan Lokal sinkron
source: '$projectRoot\AGENTS.md'
last_sync: "$($syncTime.ToString('yyyy-MM-dd HH:mm:ss K'))"
---

$agentsText
"@

    $notesPath = Join-Path $projectRoot 'Notes\Catatan.txt'
    $notesText = if (Test-Path -LiteralPath $notesPath) { Get-Content -LiteralPath $notesPath -Raw } else { 'Belum ada Notes/Catatan.txt.' }
    Write-Utf8File -Path (Join-Path $vaultProjectPath '03 - Catatan Proyek (sinkron).md') -Content @"
---
title: Nad - Catatan Proyek sinkron
source: '$notesPath'
last_sync: "$($syncTime.ToString('yyyy-MM-dd HH:mm:ss K'))"
---

$notesText
"@

    $auditNote = @"
---
title: Nad - Audit Pin GPIO
source: '$projectRoot\hardware\pin-audit\audit_pin_gpio.xlsx'
last_sync: "$($syncTime.ToString('yyyy-MM-dd HH:mm:ss K'))"
---

# Audit Pin GPIO

Workbook audit: [[Lampiran/audit_pin_gpio.xlsx]]

## Ringkasan saat audit terakhir

- Bentrok GPIO12: Flow A dan trigger ultrasonik pada skematik.
- GPIO17: dipakai bersama oleh beberapa trigger level pada skematik.
- GPIO40: muncul pada lebih dari satu jalur echo level.
- GPIO10/11/18: label ganda antara ESP32 dan 74HC595 merupakan koneksi bus yang sama, bukan bentrok otomatis.
- BH1750: firmware mengharapkan I2C 0x23; pastikan perangkatnya benar-benar ada pada rakitan.

Untuk keputusan akhir, gunakan workbook dan ukur koneksi fisik; label pada skematik saja tidak membuktikan semua konektor terpasang bersamaan.
"@
    Write-Utf8File -Path (Join-Path $vaultProjectPath '04 - Audit Pin GPIO.md') -Content $auditNote

    $auditWorkbook = Join-Path $projectRoot 'hardware\pin-audit\audit_pin_gpio.xlsx'
    if (Test-Path -LiteralPath $auditWorkbook) {
        Copy-Item -LiteralPath $auditWorkbook -Destination (Join-Path $attachmentPath 'audit_pin_gpio.xlsx') -Force
    }

    $sourceState = Get-FileState
    $stateLines = foreach ($entry in $sourceState.GetEnumerator()) {
        '{0}{1}{2}' -f $entry.Key, [char]9, $entry.Value
    }
    Write-Utf8File -Path (Join-Path $vaultProjectPath '_sync-state.tsv') -Content ($stateLines -join [Environment]::NewLine)
    Write-Output "Synced: $vaultProjectPath"
}

function Install-ObsidianWatcher {
    Sync-Obsidian
    $powershellPath = (Get-Command powershell.exe).Source
    $argumentString = '-NoProfile -ExecutionPolicy Bypass -File "{0}" -VaultPath "{1}" -Watch' -f $scriptPath, $VaultPath
    $action = New-ScheduledTaskAction -Execute $powershellPath -Argument $argumentString
    $trigger = New-ScheduledTaskTrigger -AtLogOn
    $principal = New-ScheduledTaskPrincipal -UserId "$env:USERDOMAIN\$env:USERNAME" -LogonType Interactive -RunLevel Limited
    $installedBy = 'Scheduled Task'
    try {
        Register-ScheduledTask -TaskName $watcherTaskName -Action $action -Trigger $trigger -Principal $principal -Force | Out-Null
    } catch {
        $startupPath = [Environment]::GetFolderPath('Startup')
        $startupCommandPath = Join-Path $startupPath 'Nad Obsidian Sync.cmd'
        $startupCommand = @"
@echo off
start "" /min "$powershellPath" -NoProfile -ExecutionPolicy Bypass -File "$scriptPath" -VaultPath "$VaultPath" -Watch
"@
        Set-Content -LiteralPath $startupCommandPath -Value $startupCommand -Encoding ascii
        $installedBy = 'User Startup'
    }
    Start-Process -FilePath $powershellPath -WindowStyle Hidden -ArgumentList @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $scriptPath, '-VaultPath', $VaultPath, '-Watch')
    Write-Output "Installed and started: $watcherTaskName ($installedBy)"
}

function Uninstall-ObsidianWatcher {
    Unregister-ScheduledTask -TaskName $watcherTaskName -Confirm:$false -ErrorAction SilentlyContinue
    $startupCommandPath = Join-Path ([Environment]::GetFolderPath('Startup')) 'Nad Obsidian Sync.cmd'
    if (Test-Path -LiteralPath $startupCommandPath) {
        Remove-Item -LiteralPath $startupCommandPath -Force
    }
    Write-Output "Removed: $watcherTaskName"
}

if ($UninstallWatcher) {
    Uninstall-ObsidianWatcher
    exit 0
}
if ($InstallWatcher) {
    Install-ObsidianWatcher
    exit 0
}
if ($Watch) {
    $lastState = $null
    while ($true) {
        $currentState = Get-FileState
        $currentSerialized = ($currentState.GetEnumerator() | ForEach-Object { '{0}={1}' -f $_.Key, $_.Value }) -join [Environment]::NewLine
        if ($null -eq $lastState -or $currentSerialized -ne $lastState) {
            Sync-Obsidian
            $lastState = $currentSerialized
        }
        Start-Sleep -Seconds ([Math]::Max(1, $PollSeconds))
    }
}
Sync-Obsidian
