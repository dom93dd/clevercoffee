/**
 * @file descaleProgram.h
 *
 * @brief Orione build: the descaling program (lib/Orione/src/OrioneDescale.h) on the machine. Started, continued
 *        ("Weiter") and stopped from the web page (POST /descale), applied in loop(). While it runs the machine stays
 *        in the flush state (no brew in between), the heater is off (loopPid()), valve and pump switch together. The
 *        brew switch, an alarm or a sensor error end it. At its end the descaling counter starts again at 0.
 *        Only with the water level sensor: it ends the runs.
 */

#pragma once

#include <OrioneDescale.h>

namespace descale {

    inline orione::DescaleProgram program;
    inline bool flowing = false;

    enum Request : int8_t {
        kNoRequest = 0,
        kStart = 1,
        kNext = 2,
        kStop = -1,
    };

    inline portMUX_TYPE requestLock = portMUX_INITIALIZER_UNLOCKED;
    inline int8_t request = kNoRequest; // set by the web server task

    inline bool running() {
        return program.running();
    }

    inline bool sensorEnabled() {
        return config.get<bool>("hardware.sensors.watertank.enabled");
    }

    /** Ready to start: sensor, a full tank, the machine idle (heating on or off), nothing else running */
    inline bool canStart() {
        return sensorEnabled() && waterTankFull && (machineState == kPidNormal || machineState == kPidDisabled) && !checkBrewActive() && !backflushOn &&
               !warmup_flush::flush.running() && currBrewSwitchState == kBrewSwitchIdle;
    }

    /** web server task: applied by the next loop() */
    inline void requestFromWeb(const Request r) {
        portENTER_CRITICAL(&requestLock);
        request = r;
        portEXIT_CRITICAL(&requestLock);
    }

    inline void setFlow(const bool flow) {
        if (flow == flowing) {
            return;
        }

        flowing = flow;

        // valve and pump always together: the valve alone makes the Pulsor board pulse the pump
        if (flow) {
            valveRelay->on();
            pumpRelay->on();
        }
        else {
            pumpRelay->off();
            valveRelay->off();
        }
    }

    inline void end(const char* why) {
        setFlow(false);
        program.stop();

        if (machineState == kManualFlush) {
            machineState = pidON ? kPidNormal : kPidDisabled;
        }

        resetStandbyTimer(machineState); // over an hour in the flush state: no standby right after it
        LOG(INFO, why);
    }

    /** Call from loop() after handleMachineState() and before valveSafetyShutdownCheck() */
    inline void loop() {
        portENTER_CRITICAL(&requestLock);
        const int8_t wanted = request;
        request = kNoRequest;
        portEXIT_CRITICAL(&requestLock);

        if (wanted == kStart && !program.running()) {
            if (canStart()) {
                program.start(millis());
                machineState = kManualFlush;
                LOG(INFO, "Descaling started: heater off");
            }
            else {
                LOG(WARNING, "Descaling: cannot start now (water level sensor, tank, machine busy)");
            }
        }
        else if (wanted == kNext) {
            if (!program.next(millis(), waterTankFull)) {
                LOG(WARNING, "Descaling: nothing to continue (or the tank is empty)");
            }
        }
        else if (wanted == kStop && program.running()) {
            end("Descaling stopped from the web page");
            return;
        }

        if (!program.running()) {
            if (program.phase() == orione::DescaleProgram::kDone) {
                care::descaled(shot_history::nowUtc());
                end("Descaling done: counter reset");
            }

            return;
        }

        if (currBrewSwitchState != kBrewSwitchIdle) {
            brewSwitchWasOff = false; // no shot with the descaler: only after the switch went off again
            end("Descaling stopped: brew switch");
            return;
        }

        if (machineState != kManualFlush) {
            end("Descaling stopped: the machine left the flush state (alarm, sensor error)");
            return;
        }

        const auto before = program.phase();
        const int beforeRound = program.round(), beforePass = program.pass();
        setFlow(program.update(millis(), {waterTankFull, static_cast<float>(temperature)}));

        if (program.phase() != before || program.round() != beforeRound || program.pass() != beforePass) {
            LOGF(INFO, "Descaling: phase %d, round %d, pass %d", static_cast<int>(program.phase()), program.round(), program.pass());
        }
    }

} // namespace descale
