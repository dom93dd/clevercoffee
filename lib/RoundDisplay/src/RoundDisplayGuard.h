/**
 * @file RoundDisplayGuard.h
 *
 * @brief Decision of the loop guard in the round display builds (glue: src/display/roundLoopGuard.h).
 *
 * The heater is switched by a timer interrupt with the last PID output, while loop() computes
 * that output and draws the display. If loop() hung, the heater would keep its last duty cycle,
 * up to 100 %, and the overtemperature stop (also in loop()) could not act. A watcher task on the
 * other core asks this function a few times per second.
 *
 * loop() stops for a while on purpose in a few places, so the limits lie above those:
 *   Bluetooth scale connecting: blocking connect with 5 s timeout plus service discovery
 *   scale tare ~4 s, scale calibration ~16 s (delay() calls in scaleHandler.h)
 *   OTA upload 30-90 s: the firmware stops the heater timer itself, the guard then stays out
 */

#pragma once

#include <cstdint>

namespace rd {

    enum class GuardAction : uint8_t {
        None,
        HoldHeater,                             // keep the heater off until loop() runs again
        Restart,                                // loop() does not come back: restart (the heater is off after a restart)
    };

    constexpr uint32_t kGuardHoldMs = 8000;     // above the Bluetooth scale connect
    constexpr uint32_t kGuardRestartMs = 30000; // above the scale calibration

    /**
     * @param nowMs         current time
     * @param lastBeatMs    time of the last loop() start, 0 while loop() has not run yet
     * @param heaterTimerOn heater interrupt running (off during OTA updates)
     */
    inline GuardAction loopGuardAction(const uint32_t nowMs, const uint32_t lastBeatMs, const bool heaterTimerOn) {
        if (lastBeatMs == 0 || !heaterTimerOn) {
            return GuardAction::None;
        }

        const uint32_t stalled = nowMs - lastBeatMs; // also right across the millis() overflow

        if (stalled >= kGuardRestartMs) {
            return GuardAction::Restart;
        }

        return stalled >= kGuardHoldMs ? GuardAction::HoldHeater : GuardAction::None;
    }

} // namespace rd
