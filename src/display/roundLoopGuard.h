/**
 * @file roundLoopGuard.h
 *
 * @brief Keeps the heater off while loop() hangs; restarts if it does not come back.
 *        Only in builds with -D ROUND_DISPLAY. Decision and limits: lib/RoundDisplay/src/RoundDisplayGuard.h.
 *
 * loop() calls loopGuardBeat() first thing. A small task on core 0 (loop() runs on core 1) checks
 * every 200 ms how long the last beat is ago. While the guard holds, the heater interrupt in isr.h
 * keeps the relay off, so a hanging display (or anything else in loop()) cannot leave the heater on.
 */

#pragma once

#include <RoundDisplayGuard.h>
#include <esp_system.h>

bool isTimer1Enabled();                           // isr.h

inline volatile bool loopGuardHoldHeater = false; // read by the heater interrupt
inline volatile uint32_t loopGuardLastBeat = 0;
inline volatile uint32_t loopGuardHolds = 0;      // count of holds, written by the watcher
inline volatile uint32_t loopGuardStalled = 0;    // longest stall of the last hold in ms

/**
 * @brief Called at the start of every loop(); reports a hold that just ended
 */
inline void loopGuardBeat() {
    static uint32_t reportedHolds = 0;
    const uint32_t now = millis();
    loopGuardLastBeat = now == 0 ? 1 : now;

    if (loopGuardHolds != reportedHolds) {
        reportedHolds = loopGuardHolds;
        LOGF(WARNING, "loop() stood still for %lu ms, the heater was kept off meanwhile", static_cast<unsigned long>(loopGuardStalled));
    }
}

inline void loopGuardTask(void*) {
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(200));

        // The beat first, then the clock: now is never older than the beat it is compared with
        const uint32_t beat = loopGuardLastBeat;
        const uint32_t now = millis();

        switch (rd::loopGuardAction(now, beat, isTimer1Enabled())) {
            case rd::GuardAction::Restart:
                loopGuardHoldHeater = true;
                ets_printf("loop() stood still for %u ms, restarting\n", static_cast<unsigned>(now - beat));
                esp_restart();
                break;

            case rd::GuardAction::HoldHeater:
                if (!loopGuardHoldHeater) {
                    loopGuardHoldHeater = true;
                    ++loopGuardHolds;
                }

                loopGuardStalled = now - beat;
                break;

            default:
                loopGuardHoldHeater = false;
                break;
        }
    }
}

/**
 * @brief Starts the watcher (once, at the end of setup())
 */
inline void loopGuardStart() {
    xTaskCreatePinnedToCore(loopGuardTask, "loopGuard", 1536, nullptr, 2, nullptr, 0); // used ~0.7 KB at most (GET /boot)
}
