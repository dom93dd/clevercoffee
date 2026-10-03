/**
 * Heap experiment for the Bluetooth scale (env ble_leak): which part of what AcaiaArduinoBLE does
 * while no scale is around loses memory. Prints one line per step over the serial port:
 *   A  NimBLE stack down and up (the library's "deep cleanup"; unfixed, it ran after every scan)
 *   B  scan start / stop / clear only (a scan restart, what the fixed library does every 15 s)
 *   C  the library itself, driven like the firmware's scale task (updateConnection every 250 ms)
 * First without WiFi, then with WiFi from the credentials saved in NVS (nothing is written).
 * Env ble_leak_fixed (-D ONLY_LIBRARY): only C, with the fixed library lib/OrioneScaleBLE.
 */

#include <Arduino.h>
#include <AcaiaArduinoBLE.h>
#include <NimBLEDevice.h>
#include <WiFi.h>

#ifndef LIBRARY_MINUTES
#define LIBRARY_MINUTES 12
#endif

namespace {
    size_t freeHeap() {
        return heap_caps_get_free_size(MALLOC_CAP_8BIT);
    }

    class Counter : public NimBLEScanCallbacks {
        public:
            void onResult(const NimBLEAdvertisedDevice*) override {
                ++results;
            }

            volatile uint32_t results = 0;
    } counter;

    void scanLikeTheLibrary(NimBLEScan* s) {
        s->setScanCallbacks(&counter, true);
        s->setActiveScan(true);
        s->setInterval(500);
        s->setWindow(100);
        s->setMaxResults(0);
        s->setDuplicateFilter(false);
    }

    void phaseA(const char* tag, const int cycles) {
        NimBLEDevice::init("");
        delay(500);
        const size_t start = freeHeap();

        for (int i = 1; i <= cycles; ++i) {
            NimBLEDevice::deinit(true);
            delay(500);
            NimBLEDevice::init("");
            delay(200);
            NimBLEScan* s = NimBLEDevice::getScan();
            scanLikeTheLibrary(s);
            s->start(0);
            delay(1000);
            s->stop();
            s->clearResults();
            delay(300);
            Serial.printf("%s A %2d free %6u diff %6d results %lu\n", tag, i, freeHeap(), static_cast<int>(freeHeap()) - static_cast<int>(start), static_cast<unsigned long>(counter.results));
        }
    }

    void phaseB(const char* tag, const int cycles) {
        NimBLEScan* s = NimBLEDevice::getScan();
        scanLikeTheLibrary(s);
        const size_t start = freeHeap();

        for (int i = 1; i <= cycles; ++i) {
            s->start(0);
            delay(2000);
            s->stop();
            s->clearResults();
            delay(300);

            if (i % 5 == 0) {
                Serial.printf("%s B %2d free %6u diff %6d results %lu\n", tag, i, freeHeap(), static_cast<int>(freeHeap()) - static_cast<int>(start), static_cast<unsigned long>(counter.results));
            }
        }
    }

    void phaseC(const char* tag, const uint32_t minutes) {
        NimBLEDevice::deinit(true);
        delay(500);
        auto* scale = new AcaiaArduinoBLE(false);
        scale->init();
        delay(1000);
        const size_t start = freeHeap();
        const uint32_t t0 = millis();
        uint32_t lastPrint = t0;
        int restarts = 0;
        bool wasScanning = scale->isConnecting();

        while (millis() - t0 < minutes * 60000UL) {
            scale->updateConnection();
            const bool scanning = scale->isConnecting();

            if (scanning && !wasScanning) {
                ++restarts; // FAILED -> SCANNING (unfixed library: after every scan, with a stack reset)
                delay(1500);
                Serial.printf("%s C restart %3d free %6u diff %6d\n", tag, restarts, freeHeap(), static_cast<int>(freeHeap()) - static_cast<int>(start));
            }

            wasScanning = scanning;

            if (millis() - lastPrint >= 60000) {
                lastPrint = millis();
                Serial.printf("%s C minute %2lu free %6u diff %6d min %u\n", tag, static_cast<unsigned long>((millis() - t0) / 60000), freeHeap(), static_cast<int>(freeHeap()) - static_cast<int>(start), heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT));
            }

            delay(250);
        }
    }
}

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.printf("\nBLE heap experiment, free %u\n", freeHeap());

#ifndef ONLY_LIBRARY
    phaseA("noWiFi", 20);
    phaseB("noWiFi", 40);
#endif

    WiFi.mode(WIFI_STA);
    WiFi.begin(); // credentials saved by the firmware's WiFi setup

    for (int i = 0; i < 40 && WiFi.status() != WL_CONNECTED; ++i) {
        delay(500);
    }

    Serial.printf("WiFi %s, free %u\n", WiFi.status() == WL_CONNECTED ? "connected" : "NOT connected", freeHeap());
#ifndef ONLY_LIBRARY
    phaseA("WiFi", 20);
    phaseB("WiFi", 40);
#endif
    phaseC("WiFi", LIBRARY_MINUTES);
    Serial.println("DONE");
}

void loop() {
    delay(1000);
}
