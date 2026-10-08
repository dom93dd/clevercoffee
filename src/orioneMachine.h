/**
 * @file orioneMachine.h
 *
 * @brief Orione build: what the firmware cannot switch or see directly, worked out from what it can.
 *        Steam from the original steam switch (lib/Orione/src/OrioneHeat.h, SteamWatch) for the display
 *        and the web page, and why the ESP32 last started (power, brownout, crash, watchdog, the heap
 *        safety net), kept for the maintenance tab: an unexplained restart at the first start in the
 *        machine (08.10.2026) left no trace without a serial log.
 */

#pragma once

#include <OrioneHeat.h>
#include <esp_system.h>

namespace orione_machine {

    inline orione::SteamWatch steam;

    /** Call from loop(): the brew temperature against the brew setpoint */
    inline void loop() {
        steam.update(temperature, brewSetpoint);

        if (backflushSwitchReminder && currBrewSwitchState == kBrewSwitchIdle) {
            backflushSwitchReminder = false; // switched off after the backflush
        }

        // cleaning with detergent: asked for by the page, over when backflush mode goes off (at its end or by hand)
        if (care::cleaningRequested) {
            care::cleaningRequested = false;

            if (currBackflushState == kBackflushIdle) {
                backflushOn = true;
                care::cleaning.start();
                LOG(INFO, "Cleaning with detergent: backflush mode on");
            }
        }
        else if (!backflushOn && care::cleaning.phase() != orione::CleaningProgram::kOff) {
            care::cleaning.cancel();
            LOG(INFO, "Cleaning cancelled");
        }
    }

    // Survives a software restart (not a power loss): set just before a deliberate one, read at the start
    constexpr uint32_t kRestartHeap = 0x48454150; // "HEAP"
    inline RTC_NOINIT_ATTR uint32_t restartMark;
    inline const char* startReason = "unknown";

    /** Call once at the start of setup() */
    inline void noteStart() {
        const esp_reset_reason_t r = esp_reset_reason();

        switch (r) {
            case ESP_RST_POWERON:
                startReason = "power";
                break;
            case ESP_RST_EXT:
                startReason = "reset";
                break;
            case ESP_RST_SW:
                startReason = restartMark == kRestartHeap ? "heap" : "software";
                break;
            case ESP_RST_PANIC:
                startReason = "crash";
                break;
            case ESP_RST_INT_WDT:
            case ESP_RST_TASK_WDT:
            case ESP_RST_WDT:
                startReason = "watchdog";
                break;
            case ESP_RST_BROWNOUT:
                startReason = "brownout";
                break;
            default:
                startReason = "unknown";
                break;
        }

        restartMark = 0;
    }

    /** The heap safety net restarts: mark it, so the next start can tell */
    inline void markHeapRestart() {
        restartMark = kRestartHeap;
    }

} // namespace orione_machine
