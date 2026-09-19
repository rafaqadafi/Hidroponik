#pragma once

#include <stddef.h>
#include <stdint.h>

namespace Failsafe {

bool begin();

// Memproses command dari edge device. Command ON wajib memiliki heartbeat
// yang masih sehat dan kondisi float switch yang aman.
bool handleControlPayload(const char *payload, size_t length);

// Heartbeat berasal dari Rule Engine di edge device.
void handleHeartbeat();

// Transport MQTT dan status local safety dipasok oleh task aplikasi.
void setMqttConnected(bool connected);
void updateLocalSafety(bool allFloatsNormal);

// Watchdog task aplikasi: jika outputTask hang, hardware watchdog mereset ESP32.
void attachWatchdog();
void feedWatchdog();

} // namespace Failsafe
