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
#include <cstdint>

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

    /**
     * Safety net behind the brake: when the heap stays below the brake's limits for a minute (it
     * fragments or something leaks), every page request is refused and only a restart helps. Then
     * restart, but never during a shot or a backflush (busy).
     */
    class HeapWatch {
        public:
            static constexpr uint32_t kGraceMs = 60000;

            /** @return true: restart now */
            bool update(const uint32_t nowMs, const bool low, const bool busy) {
                if (!low) {
                    since_ = 0;
                    return false;
                }

                if (since_ == 0) {
                    since_ = nowMs == 0 ? 1 : nowMs;
                }

                return !busy && nowMs - since_ >= kGraceMs;
            }

            /** heap below the brake's limits */
            static bool low(const size_t freeHeap, const size_t largestBlock, const GateLimits& l = GateLimits{}) {
                return largestBlock < l.brakeBlock || freeHeap < l.brakeFree;
            }

        private:
            uint32_t since_ = 0;
    };

} // namespace orione
