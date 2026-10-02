/**
 * @file OrioneWebGate.h
 *
 * @brief Decision of the web request gate of the Orione build (glue: src/webRequestGate.h).
 *
 * Each web response holds 10-30 KB of heap until the phone has acknowledged its data; a browser
 * opens up to six connections at once. The gate lets one response run at a time and makes the
 * others wait; an emergency brake answers "busy" (503) or even resets the connection before the
 * heap gets so low that the WiFi driver fails (measured 02.10.2026: ESP32 dropped out of the WiFi).
 * Pure function, unit-tested (simulator/test/test_orione).
 */

#pragma once

#include <cstddef>

namespace orione {

    enum class GateAction {
        Start, // answer now
        Wait,  // pause the request, answer when a running response has finished
        Busy,  // answer 503 right away (emergency brake or queue full)
        Reset, // reset the connection without allocating anything (heap almost gone)
    };

    struct GateLimits {
            int maxActive = 1;                 // responses at the same time (two parallel bundle files took the heap to 11 KB)
            size_t maxWaiting = 5;             // a page load needs at most 4 waiting requests
            size_t secondNeedsBlock = 12 * 1024; // heap one response may need (WLED uses the same estimate)
            size_t secondNeedsFree = 20 * 1024;
            size_t brakeBlock = 8 * 1024;      // emergency brake: below this largest free block ...
            size_t brakeFree = 18 * 1024;      // ... or below this free heap: 503
            size_t abortBlock = 4 * 1024;      // below this: reset, not even a 503 any more
    };

    /**
     * @param active       responses running now
     * @param waiting      requests waiting in the queue (without this one)
     * @param freeHeap     free 8-bit heap in bytes
     * @param largestBlock largest free 8-bit block in bytes
     */
    constexpr GateAction gateDecide(const int active, const size_t waiting, const size_t freeHeap, const size_t largestBlock, const GateLimits& l = GateLimits{}) {
        if (largestBlock < l.abortBlock) {
            return GateAction::Reset;
        }

        if (largestBlock < l.brakeBlock || freeHeap < l.brakeFree) {
            return GateAction::Busy;
        }

        if (active <= 0) {
            return GateAction::Start;
        }

        if (active < l.maxActive && largestBlock >= l.secondNeedsBlock && freeHeap >= l.secondNeedsFree) {
            return GateAction::Start;
        }

        return waiting >= l.maxWaiting ? GateAction::Busy : GateAction::Wait;
    }

} // namespace orione
