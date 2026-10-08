/**
 * @file orioneLog.h
 *
 * @brief Orione build (CC_ORIONE): every log line also goes into a 4 KB ring in RTC memory
 *        (lib/Orione/src/OrioneLogRing.h). It survives a software restart, a crash and the watchdog,
 *        not a power loss, so GET /log shows the lines before an unexpected restart; development and
 *        bug hunting without the USB cable (Dominik, 08.10.2026). The live log stays on TCP port 23.
 */

#pragma once

#include <Logger.h>
#include <OrioneLogRing.h>
#include <esp_system.h>

namespace orione_log {

    inline RTC_NOINIT_ATTR orione::LogRing<4096> ring;
    inline portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;

    inline void sink(const char* time, const int level, const char* msg) {
        static constexpr const char* kLevels[] = {"T ", "D ", "I ", "W ", "E ", "F ", "S "};
        portENTER_CRITICAL(&lock);
        ring.append(time);
        ring.append(level >= 0 && level <= 6 ? kLevels[level] : "? ");
        ring.append(msg);
        ring.append("\n");
        portEXIT_CRITICAL(&lock);
    }

    /** Call first thing in setup(): continue the log after a restart, start it fresh after power-up */
    inline void begin(const char* startReason) {
        const esp_reset_reason_t r = esp_reset_reason();
        const bool kept = r != ESP_RST_POWERON && r != ESP_RST_UNKNOWN;
        portENTER_CRITICAL(&lock);
        ring.begin(kept);
        ring.append("--- start: ");
        ring.append(startReason);
        ring.append(" ---\n");
        portEXIT_CRITICAL(&lock);
        Logger::setSink(sink);
    }

    /** Bytes [from, to) of the log for a request: from the oldest kept up to now */
    inline void span(uint32_t& from, uint32_t& to) {
        portENTER_CRITICAL(&lock);
        from = ring.first();
        to = ring.written;
        portEXIT_CRITICAL(&lock);
    }

    inline size_t read(const uint32_t pos, const uint32_t to, char* out, const size_t max) {
        portENTER_CRITICAL(&lock);
        const size_t n = ring.read(pos, to, out, max > 512 ? 512 : max); // short: interrupts are off meanwhile
        portEXIT_CRITICAL(&lock);
        return n;
    }

} // namespace orione_log
