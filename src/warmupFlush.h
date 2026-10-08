/**
 * @file warmupFlush.h
 *
 * @brief Orione build: the warm-up flush (lib/Orione/src/OrioneWarmupFlush.h) on the machine. Takes its
 *        inputs from the firmware's state, switches valve and pump together, holds the machine in the
 *        flush state while it runs (display "SPÜLEN", no brew in between) and applies start and stop
 *        from the web page in loop().
 */

#pragma once

#include <OrioneWarmupFlush.h>

namespace warmup_flush {

    inline orione::WarmupFlush flush;
    inline bool flowing = false;
    inline unsigned long flowSince = 0;

    inline portMUX_TYPE requestLock = portMUX_INITIALIZER_UNLOCKED;
    inline int8_t request = 0; // set by the web server task: 1 start, -1 stop

    inline bool sensorEnabled() {
        return config.get<bool>("hardware.sensors.watertank.enabled");
    }

    /** for the web page: "waiting" only when it will really run by itself (sensor and setting on) */
    inline int livePhase() {
        const auto phase = flush.phase();
        const bool automatic = sensorEnabled() && config.get<bool>("brew.warmup_flush");
        return phase == orione::WarmupFlush::kWaiting && !automatic ? orione::WarmupFlush::kNone : phase;
    }

    /** web server task: start or stop by hand, applied by the next loop() */
    inline void requestFromWeb(const bool start) {
        portENTER_CRITICAL(&requestLock);
        request = start ? 1 : -1;
        portEXIT_CRITICAL(&requestLock);
    }

    /** Call from loop() after handleMachineState() and before valveSafetyShutdownCheck() */
    inline void loop() {
        portENTER_CRITICAL(&requestLock);
        const int8_t wanted = request;
        request = 0;
        portEXIT_CRITICAL(&requestLock);

        if (wanted > 0) {
            flush.requestStart();
        }
        else if (wanted < 0) {
            flush.requestStop();
        }

        const bool wasRunning = flush.running();
        const bool userActive = currBrewSwitchState != kBrewSwitchIdle || machineState == kBrew || machineState == kBackflush || machineState == kHotWater ||
                                machineState == kSteam || backflushOn;
        const bool ready = machineState == kPidNormal || (machineState == kManualFlush && wasRunning);
        const bool flow = flush.update(millis(), temperature, brewSetpoint, {sensorEnabled(), config.get<bool>("brew.warmup_flush"), waterTankFull, ready, userActive});

        if (flow != flowing) {
            flowing = flow;

            // valve and pump always together: the valve alone makes the Pulsor board pulse the pump
            if (flow) {
                valveRelay->on();
                pumpRelay->on();
                flowSince = millis();
            }
            else {
                pumpRelay->off();
                valveRelay->off();
                care::rinse((millis() - flowSince) / 1000.0f); // water through the block, for the descaling reminder
            }
        }

        if (flush.running()) {
            if (!wasRunning) {
                LOG(INFO, "Warm-up flush started");
                machineState = kManualFlush;

                if (standbyModeOn) {
                    resetStandbyTimer(machineState);
                }
            }

            currBrewTime = flush.elapsedMs(millis());
        }
        else if (wasRunning) {
            LOGF(INFO, "Warm-up flush over after %lu s", static_cast<unsigned long>(currBrewTime / 1000));
            currBrewTime = 0;
            shot_history::flushed();

            if (machineState == kManualFlush) {
                machineState = kPidNormal;
            }
        }
    }

} // namespace warmup_flush
