/*
  AcaiaArduinoBLE.h - Library for connecting to
  an Acaia Scale using the ArduinoBLE library.
  Created by Tate Mazer, December 13, 2023.
  Released into the public domain.

  Pio Baettig: Adding Felicita Arc support

  Known Bugs:
    * Only supports Grams
*/
#pragma once

#define WRITE_CHAR_OLD_VERSION "2a80"
#define READ_CHAR_OLD_VERSION  "2a80"
#define WRITE_CHAR_NEW_VERSION "49535343-8841-43f4-a8d4-ecbe34729bb3"
#define READ_CHAR_NEW_VERSION  "49535343-1e4d-4bd9-ba61-23c647249616"
#define WRITE_CHAR_DECENT      "000036F5-0000-1000-8000-00805F9B34FB"
#define READ_CHAR_DECENT       "0000FFF4-0000-1000-8000-00805F9B34FB"
#define SUUID_DECENTSCALE      "0000FFF0-0000-1000-8000-00805F9B34FB"
#define SUUID_GENERIC          "ff10"
#define SUUID_BOOKOO           "0ffe"
#define SUUID_WEIGHMYBRU       "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define WRITE_CHAR_GENERIC     "ff12"
#define READ_CHAR_GENERIC      "ff11"
#define WRITE_CHAR_BOOKOO      "ff12"  // Same as GENERIC
#define READ_CHAR_BOOKOO       "ff11"  // Same as GENERIC
#define WRITE_CHAR_WEIGHMYBRU  "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"
#define READ_CHAR_WEIGHMYBRU   "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"
#define HEARTBEAT_PERIOD_MS    2750
#define MAX_PACKET_PERIOD_MS   5000

#include "Arduino.h"
#include <NimBLEAdvertisedDevice.h>
#include <NimBLEClient.h>
#include <NimBLEDevice.h>
#include <NimBLEUtils.h>
#include <OrioneScaleList.h> // Orione: the scales a search found (lib/Orione)

#include <utility>

enum scale_type {
    OLD, // Lunar (pre-2021)
    NEW, // Lunar (2021), Pyxis
    GENERIC, // Felicita Arc, etc
    DECENT, // Decent Scale + EspressiScale
    BOOKOO, // Bookoo Themis and Themis Ultra
    WEIGHMYBRU // WeighMyBru DIY scales
};

enum ConnectionState {
    IDLE,
    SCANNING,
    CONNECTING,
    DISCOVERING,
    CONFIGURING,
    CONNECTED,
    FAILED
};

class MyAdvertisedDeviceCallbacks : public NimBLEScanCallbacks {
    public:
        void onResult(const NimBLEAdvertisedDevice *advertisedDevice) override;

        void onScanEnd(const NimBLEScanResults &scanResults, int reason) override;

        [[nodiscard]] bool hasFoundDevice() const {
            return _deviceFound;
        }

        [[nodiscard]] NimBLEAddress getFoundDeviceAddress() const {
            return _foundDeviceAddress;
        }

        [[nodiscard]] String getFoundDeviceName() const {
            return _foundDeviceName;
        }

        void clearFoundDevice() {
            _deviceFound = false;
        }

        // Orione: the target is read by the NimBLE task and set by others: a fixed buffer under a lock
        // instead of a String (whose reallocation could race with the read)
        void setTargetMac(const String& mac) {
            portENTER_CRITICAL(&_lock);
            std::strncpy(_target, mac.c_str(), sizeof(_target) - 1);
            _target[sizeof(_target) - 1] = '\0';
            portEXIT_CRITICAL(&_lock);
        }

        // Orione: without a chosen scale, connect to none (instead of the first supported one)
        void setRequireTarget(const bool require) {
            _requireTarget = require;
        }

        // Orione: collect every supported scale for the web page until untilMs
        void discover(const uint32_t untilMs) {
            portENTER_CRITICAL(&_lock);
            _found.clear();
            _discoverUntil = untilMs;
            portEXIT_CRITICAL(&_lock);
        }

        [[nodiscard]] bool discovering() const {
            return static_cast<int32_t>(_discoverUntil - millis()) > 0;
        }

        int found(orione::FoundScale *out, const int max) {
            portENTER_CRITICAL(&_lock);
            const int n = _found.list(out, max, millis());
            portEXIT_CRITICAL(&_lock);
            return n;
        }

        void setDebug(const bool debug) {
            _debug = debug;
        }

    private:
        bool _deviceFound = false;
        NimBLEAddress _foundDeviceAddress = {};
        String _foundDeviceName;
        char _target[18] = {}; // Orione: see setTargetMac()
        bool _requireTarget = false;
        volatile uint32_t _discoverUntil = 0;
        orione::ScaleList _found;
        portMUX_TYPE _lock = portMUX_INITIALIZER_UNLOCKED;
        bool _debug = false;

        static bool isSupportedScale(const String &name);
};

class MyClientCallback : public NimBLEClientCallbacks {
    public:
        void onConnect(NimBLEClient *pclient) override;

        void onDisconnect(NimBLEClient *pclient, int reason) override;

        void setDebug(const bool debug) {
            _debug = debug;
        }

    private:
        bool _debug = false;
};

class AcaiaArduinoBLE {
    public:
        explicit AcaiaArduinoBLE(bool debug);

        ~AcaiaArduinoBLE();

        bool init(const String & = "");
        bool updateConnection();
        [[nodiscard]] bool isConnecting() const;
        void tare();
        void startTimer() const;
        void stopTimer() const;
        void resetTimer() const;
        bool heartbeat();
        [[nodiscard]] float getWeight() const;
        [[nodiscard]] bool heartbeatRequired() const;
        [[nodiscard]] bool isConnected() const;
        bool newWeightAvailable();

        // Orione: choosing the scale from the web page
        void setTarget(const String &mac); // "aa:bb:cc:dd:ee:ff", "" = none; drops a connection to another one
        void requireTarget(bool require);  // without a target, connect to none
        void discover(uint32_t ms);        // collect the supported scales around for ms
        [[nodiscard]] bool discovering() const;
        int found(orione::FoundScale *out, int max);
        [[nodiscard]] int getBattery() const; // Orione: percent, -1 unknown

    private:
        static void staticNotifyCallback(NimBLERemoteCharacteristic *pBLERemoteCharacteristic, uint8_t *pData,
                                         size_t length, bool isNotify);

        void notifyCallback(const uint8_t *pData, size_t length);
        void cleanup();
        void clearScanResults();
        void releaseClient(); // Orione: hands a client back to NimBLE

        // Debug functions
        void printData(const uint8_t data[], size_t length);
        float _currentWeight;
        NimBLEClient *_pClient;
        NimBLERemoteCharacteristic *_pWriteCharacteristic;
        NimBLERemoteCharacteristic *_pReadCharacteristic;
        NimBLEScan *_pBLEScan;
        MyAdvertisedDeviceCallbacks *_pAdvertisedDeviceCallbacks;
        MyClientCallback *_pClientCallback;

        unsigned long _lastHeartBeat;
        bool _connected;
        scale_type _type;
        bool _debug;
        unsigned long _lastPacket;
        bool _newWeightAvailable;
        volatile int _battery; // Orione: written by the NimBLE task

        uint8_t decent_scale_tare_counter;

        ConnectionState _connectionState;
        unsigned long _connectionStartTime;
        String _targetMac;
        bool _requireTarget = false; // Orione
        bool _cleanupComplete;
        unsigned long _lastScanClear;
        unsigned long _scanRestUntil = 0; // Orione: no scan until then (0: scanning or no rest due)

        int _connectionAttempts;

        static AcaiaArduinoBLE *_instance; // For static callback
};
