/**
 * @file benchSwitch.h
 *
 * @brief Bench build only (-D CC_FAKE_TEMP_SENSOR, env esp32_round_bench): a virtual brew switch
 *        instead of GPIO34. On the bare ESP32 the input floats and starts brews by itself (on the
 *        PCB a 47 kOhm pull-down, JP3, holds it low); here the pin is ignored and
 *        POST /bench/brew?s=25 holds the switch on for 25 s (s=0 releases it). Together with the
 *        simulated thermoblock this runs a whole shot on the desk: heater, pump relay, curve.
 *        POST /bench/wifi-outage?s=180 plays a router that is off for 180 s: the WiFi drops and
 *        every reconnect fails until then (checkWifi(), retryWifiWhileOffline()).
 */

#pragma once

#if defined(CC_FAKE_TEMP_SENSOR) && !defined(ROUND_TIMING)
#error "CC_FAKE_TEMP_SENSOR is for the bench build esp32_round_bench only, never for the machine"
#endif

#include <Arduino.h>

namespace bench {

    inline uint32_t brewUntilMs = 0;

    inline bool brewSwitchOn() {
        return brewUntilMs != 0 && static_cast<int32_t>(millis() - brewUntilMs) < 0;
    }

    inline void holdBrewSwitch(const uint32_t seconds) {
        brewUntilMs = seconds == 0 ? 0 : millis() + seconds * 1000 + 1; // never 0 while held
    }

    inline uint32_t wifiOutageUntilMs = 0;
    inline bool wifiOutageStart = false; // loop() drops the WiFi (not from the web server's task)

    inline bool wifiOutage() {
        return wifiOutageUntilMs != 0 && static_cast<int32_t>(millis() - wifiOutageUntilMs) < 0;
    }

    // water level sensor (the setting must be on): POST /bench/tank?empty=1
    inline bool tankEmpty = false;

} // namespace bench
