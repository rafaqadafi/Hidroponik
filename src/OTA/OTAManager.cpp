#include "OTAManager.h"

#include <Arduino.h>
#include <ArduinoOTA.h>
#include <WiFi.h>
#include "Config/config.h"

namespace {

bool ready = false;
unsigned int lastProgress = 0;

void onStart()
{
    lastProgress = 0;
    Serial.println("OTA dimulai");
}

void onEnd()
{
    Serial.println("OTA selesai, perangkat melakukan restart");
}

void onProgress(unsigned int progress, unsigned int total)
{
    if (total == 0) return;
    const unsigned int percent = progress * 100U / total;
    if (percent == 100 || percent >= lastProgress + 10) {
        Serial.printf("OTA: %u%%\n", percent);
        lastProgress = percent;
    }
}

void onError(ota_error_t error)
{
    Serial.printf("OTA gagal, kode: %u\n", error);
    if (error == OTA_AUTH_ERROR) Serial.println("OTA: autentikasi gagal");
    else if (error == OTA_BEGIN_ERROR) Serial.println("OTA: gagal memulai");
    else if (error == OTA_CONNECT_ERROR) Serial.println("OTA: gagal koneksi");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("OTA: gagal menerima data");
    else if (error == OTA_END_ERROR) Serial.println("OTA: gagal menyelesaikan update");
}

}

namespace OTAManager {

bool begin()
{
    if (ready) return true;
    if (WiFi.status() != WL_CONNECTED) return false;

    ArduinoOTA.setHostname(Config::Ota::HOSTNAME);
    ArduinoOTA.setPassword(Config::Ota::PASSWORD);
    ArduinoOTA.onStart(onStart);
    ArduinoOTA.onEnd(onEnd);
    ArduinoOTA.onProgress(onProgress);
    ArduinoOTA.onError(onError);
    ArduinoOTA.begin();

    ready = true;
    Serial.print("OTA siap pada hostname: ");
    Serial.println(Config::Ota::HOSTNAME);
    return true;
}

void handle()
{
    if (!ready) return;
    if (WiFi.status() != WL_CONNECTED) {
        ArduinoOTA.end();
        ready = false;
        return;
    }
    ArduinoOTA.handle();
}

}
