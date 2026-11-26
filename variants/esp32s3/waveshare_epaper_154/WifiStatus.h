#pragma once

#include "configuration.h"

/*

The WiFi as the Calandria screens show it, and its way out.

On ESP32, WiFi in use means NO Bluetooth: main-esp32.cpp keeps Bluetooth off while isWifiAvailable(),
which is "wifi_enabled AND a wifi_ssid" (or already connected). With wifi_enabled but no network loaded,
Bluetooth still starts. The network and its password are loaded by the app (set_config network); the
node only switches wifi_enabled, saves and reboots.

Ways back to Bluetooth, so the node is never left with neither:
  - on screen: WiFi on but not connected 3 minutes after boot (stuck()): Inicio offers to turn it off;
  - without the screen: hold the encoder button while powering on (checkEmergencyGesture(), from
    lateInitVariant: before initWifi() and before Bluetooth starts, so no reboot is needed). Not BOOT:
    GPIO0 low at reset puts the ESP32-S3 in download mode. GPIO14 is not a strapping pin.

Never logs the password.

*/

#include "NodeDB.h"
#include "RTC.h"
#include "UptimeClock.h"
#include "main.h"

#if HAS_WIFI
#include "mesh/wifi/WiFiAPClient.h"
#endif

#include <Arduino.h>
#include <string>

#ifndef WIFI_STUCK_MS
#define WIFI_STUCK_MS (3 * 60 * 1000UL)
#endif

namespace CalandriaWifi
{

inline bool enabled() { return config.network.wifi_enabled; }
inline bool haveNetwork() { return config.network.wifi_ssid[0] != '\0'; }

inline bool connected()
{
#if HAS_WIFI
    return WiFi.status() == WL_CONNECTED;
#else
    return false;
#endif
}

inline std::string ip()
{
#if HAS_WIFI
    if (connected())
        return WiFi.localIP().toString().c_str();
#endif
    return "";
}

// Why the last connection attempt failed, in plain words ("" = none yet: still trying)
inline std::string reason()
{
#if HAS_WIFI
    switch (getWifiDisconnectReason()) {
    case 0:
        return "";
    case 201: // WIFI_REASON_NO_AP_FOUND
        return "no encuentra la red";
    case 2:   // AUTH_EXPIRE
    case 15:  // 4WAY_HANDSHAKE_TIMEOUT
    case 202: // AUTH_FAIL
    case 204: // HANDSHAKE_TIMEOUT
        return "clave incorrecta";
    default:
        return "error " + std::to_string(getWifiDisconnectReason());
    }
#else
    return "";
#endif
}

// "Conectado · 192.168.0.42" / "Conectando…" / "Sin conexión (motivo)" / "Apagado"
inline std::string stateLine()
{
    if (!enabled())
        return "Apagado";
    if (connected())
        return "Conectado · " + ip();
    const std::string why = reason();
    return why.empty() ? "Conectando…" : "Sin conexión: " + why;
}

// On, with a network, and still not connected WIFI_STUCK_MS after boot
inline bool stuck() { return enabled() && haveNetwork() && !connected() && millis() > WIFI_STUCK_MS; }

// The time from NTP (WiFiAPClient syncs it every 12 h from config.network.ntp_server)
inline bool timeFromInternet() { return enabled() && getRTCQuality() >= RTCQualityNTP; }

// Switch the WiFi (saved, then a reboot: WiFi and Bluetooth are decided at boot)
inline void setEnabledAndReboot(bool on)
{
    config.network.wifi_enabled = on;
    nodeDB->saveToDisk(SEGMENT_CONFIG);
    LOG_INFO("WiFi %s from the screen: rebooting", on ? "on" : "off");
    rebootAtMsec = Time::timerEndsAtMillis(DEFAULT_REBOOT_SECONDS * 1000);
}

// Encoder button held while powering on: WiFi off, before WiFi or Bluetooth start. True if it acted.
inline bool checkEmergencyGesture()
{
#ifdef ENCODER_PIN_PRESS
    if (!enabled())
        return false;
    pinMode(ENCODER_PIN_PRESS, INPUT_PULLUP);
    delay(2);
    for (int i = 0; i < 10; i++) { // held for 300 ms, not a bounce
        if (digitalRead(ENCODER_PIN_PRESS) != LOW)
            return false;
        delay(30);
    }
    config.network.wifi_enabled = false;
    nodeDB->saveToDisk(SEGMENT_CONFIG);
    LOG_WARN("Encoder held at boot: WiFi turned off, Bluetooth comes back");
    return true;
#else
    return false;
#endif
}

} // namespace CalandriaWifi
