/**
 * @file roundTiming.h
 *
 * @brief Measurement build only (-D ROUND_TIMING, env esp32_round_timing): how long each part of
 *        loop() takes on the real chip, to find what makes the round display stutter.
 *
 * Prints once a second to the serial port (all times in ms, maxima of that second; heap in KB:
 * free now, lowest since boot, largest free block):
 *   TIMING loops <n> loop <max> outside <max> slow <loops over 20 ms> frames <n> frame <max> heap <kb> min <kb> block <kb> | <section> <max> ...
 * "outside" is the time between two loop() calls (other tasks on the same core). The print itself
 * takes a few ms; it is left out of the loop and outside times, but may lengthen one frame a second.
 * Without ROUND_TIMING everything here compiles to nothing.
 */

#pragma once

#ifdef ROUND_TIMING

#include <Arduino.h>
#include <WiFi.h>

#include "../webRequestGate.h"

namespace round_timing {

    enum Section : uint8_t {
        Logger,
        Water,
        Sensor,
        Wifi,
        Ota,
        Pid,
        Event,
        Switches,
        Model,
        Band,
        Led,
        Debug,
        Save,
        kCount
    };

    inline const char* const kNames[kCount] = {"logger", "water", "sensor", "wifi", "ota", "pid", "event", "switches", "model", "band", "led", "debug", "save"};

    inline uint32_t maxUs[kCount] = {};
    inline uint32_t loops = 0;
    inline uint32_t loopMaxUs = 0;
    inline uint32_t outsideMaxUs = 0;
    inline uint32_t slowLoops = 0;
    inline uint32_t frames = 0;
    inline uint32_t frameMaxUs = 0;
    inline uint32_t loopStartUs = 0;
    inline uint32_t loopEndUs = 0;
    inline uint32_t frameStartUs = 0;
    inline uint32_t lastPrintMs = 0;
    inline uint32_t lowHeap = UINT32_MAX; // lowest free heap seen at a loop() end this second

    inline void stacks();

    class Scope {
        public:
            explicit Scope(const Section s) :
                s_(s), t0_(micros()) {
            }

            ~Scope() {
                const uint32_t d = micros() - t0_;
                maxUs[s_] = std::max(maxUs[s_], d);
            }

        private:
            Section s_;
            uint32_t t0_;
    };

    inline void loopBegin() {
        loopStartUs = micros();

        if (loopEndUs != 0) {
            outsideMaxUs = std::max(outsideMaxUs, loopStartUs - loopEndUs);
        }
    }

    inline void loopEnd() {
        const uint32_t d = micros() - loopStartUs;
        loopMaxUs = std::max(loopMaxUs, d);
        slowLoops += d > 20000 ? 1 : 0;
        ++loops;
        lowHeap = std::min<uint32_t>(lowHeap, ESP.getFreeHeap());

        if (millis() - lastPrintMs >= 1000) {
            lastPrintMs = millis();
            char line[480];
            int n = snprintf(line, sizeof(line), "TIMING loops %u loop %.1f outside %.1f slow %u frames %u frame %.1f heap %u low %u min %u block %u |", static_cast<unsigned>(loops), loopMaxUs / 1000.0,
                             outsideMaxUs / 1000.0, static_cast<unsigned>(slowLoops), static_cast<unsigned>(frames), frameMaxUs / 1000.0, static_cast<unsigned>(ESP.getFreeHeap() / 1024),
                             static_cast<unsigned>(lowHeap / 1024), static_cast<unsigned>(ESP.getMinFreeHeap() / 1024), static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) / 1024));

#ifdef CC_ORIONE
            n += snprintf(line + n, sizeof(line) - n, " web %d/%u q%u r%u b%u sse %u/%u |", web_gate::active, static_cast<unsigned>(web_gate::waiting.size()), static_cast<unsigned>(web_gate::queuedTotal),
                          static_cast<unsigned>(web_gate::rejectedTotal), static_cast<unsigned>(web_gate::brakedTotal), static_cast<unsigned>(web_gate::sseClients), static_cast<unsigned>(web_gate::sseWaiting));
#endif

            for (int i = 0; i < kCount && n > 0 && n < static_cast<int>(sizeof(line)); ++i) {
                n += snprintf(line + n, sizeof(line) - n, " %s %.1f", kNames[i], maxUs[i] / 1000.0);
                maxUs[i] = 0;
            }

            Serial.println(line);

            static uint32_t seconds = 0;

            if (++seconds == 10 || seconds % 60 == 0) {
                stacks();
            }

            lowHeap = UINT32_MAX;
            loops = loopMaxUs = outsideMaxUs = slowLoops = frames = frameMaxUs = 0;
        }

        loopEndUs = micros(); // after the print, so it does not count as time outside loop()
    }

    /** Free heap and largest block at a point of setup() */
    inline void heapMark(const char* label) {
        Serial.printf("TIMING heap %-14s %6u block %6u wifi mode %d\n", label, static_cast<unsigned>(ESP.getFreeHeap()), static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)),
                      static_cast<int>(WiFi.getMode()));
    }

    /** Unused stack of the busy tasks (bytes never touched since start) */
    inline void stacks() {
        char line[400];
        int n = snprintf(line, sizeof(line), "TIMING stack free");

        for (const char* name : {"loopTask", "async_tcp", "sse", "scale", "nimble_host", "btController", "arduino_events", "mdns", "tiT", "wifi", "esp_timer", "Tmr Svc", "sys_evt", "ipc0", "ipc1", "IDLE"}) {
            if (TaskHandle_t t = xTaskGetHandle(name); t != nullptr && n > 0 && n < static_cast<int>(sizeof(line))) {
                n += snprintf(line + n, sizeof(line) - n, " %s %u", name, static_cast<unsigned>(uxTaskGetStackHighWaterMark(t)));
            }
        }

        Serial.println(line);
    }

    /** Called when a frame's first band is drawn and when its last band is done */
    inline void frameBegin() {
        frameStartUs = micros();
    }

    inline void frameEnd() {
        frameMaxUs = std::max<uint32_t>(frameMaxUs, micros() - frameStartUs);
        ++frames;
    }

} // namespace round_timing

#define ROUND_TIMING_CONCAT2(a, b) a##b
#define ROUND_TIMING_CONCAT(a, b)  ROUND_TIMING_CONCAT2(a, b)
#define ROUND_TIME(section)        const round_timing::Scope ROUND_TIMING_CONCAT(roundTimingScope_, __LINE__)(round_timing::section)
#define ROUND_TIMING_DO(statement) statement

#else

#define ROUND_TIME(section)
#define ROUND_TIMING_DO(statement)

#endif
