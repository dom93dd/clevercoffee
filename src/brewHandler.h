/**
 * @file brewHandler.h
 *
 * @brief Handler for brewing
 *
 */
// TODO:
//  show sections on website only if needed
//  add pressure to shot timer?
//  backflush also as bool, enable from website over diffrent var
//  SteamOn also as bool, rethink enable from website

#pragma once

#include "brewStates.h"
#include "scaleHandler.h"

// Brew control states
inline BrewSwitchState currBrewSwitchState = kBrewSwitchIdle;
inline BrewState currBrewState = kBrewIdle;
inline ManualFlushState currManualFlushState = kManualFlushIdle;
inline BackflushState currBackflushState = kBackflushIdle;

inline uint8_t brewSwitchReading = LOW;
inline uint8_t currReadingBrewSwitch = LOW;
inline bool brewSwitchWasOff = false;

// Brew values
inline double targetBrewTime = TARGET_BREW_TIME;          // brew time in s
inline double preinfusion = PRE_INFUSION_TIME;            // preinfusion time in s
inline double preinfusionPause = PRE_INFUSION_PAUSE_TIME; // preinfusion pause time in s
inline double totalTargetBrewTime = 0;                    // total target brew time including preinfusion and preinfusion pause
inline double currBrewTime = 0;                           // current running total brewed time
inline unsigned long startingTime = 0;                    // start time of brew
inline bool brewPidDisabled = false;                      // is PID disabled for delay after brew has started?
#ifdef CC_ORIONE
inline bool brewStoppedByWeight = false; // the last shot stopped at its target weight (minus the lead)
inline bool brewEndedSwitchOn = false;   // the switch waits for release because a brew (not a backflush) stopped by itself
inline bool brewWeightFallback = false;  // this shot: by weight, but without the scale, so the target time ends it
inline bool backflushCompleted = false;  // all cycles ran: backflush mode goes off (kBackflushFinished)
inline bool backflushSwitchReminder = false; // Orione: all cycles ran and the brew switch is still on: display and page say so
inline bool rinseRun = false;                // Orione: this run of the brew switch is the rinse after a shot (no pre-infusion)
constexpr double kBrewMaxSeconds = 60.0;  // no shot runs longer (unless the target time is longer)
#endif

// Backflush values
inline int backflushCycles = BACKFLUSH_CYCLES;
inline double backflushFillTime = BACKFLUSH_FILL_TIME;
inline double backflushFlushTime = BACKFLUSH_FLUSH_TIME;
inline bool backflushOn = false;
inline int currBackflushCycles = 1;

bool isPowerSwitchOperationAllowed();

/**
 * @brief True if in an intermediate brew state, false if idle or finished
 */
inline bool checkBrewActive() {
    return (currBrewState != kBrewIdle && currBrewState != kBrewFinished); // removed && !(machineState >= kEmergencyStop)
}

/**
 * @brief True if in a machine state related to brew or flush, false if in other states
 */
inline bool checkBrewStates() {
    return (machineState == kBrew || machineState == kBackflush || machineState == kManualFlush);
}

/**
 * @brief turns off valve if not in an active brew state or if machineState changes away from one related to brewing or flushing
 */
inline void valveSafetyShutdownCheck() {
    if (!checkBrewActive() && !checkBrewStates()) {
        valveRelay->off();
    }
}

#ifdef CC_ORIONE
/** @brief A scale switched on in the settings and connected */
inline bool brewScaleReady() {
    return scale && config.get<bool>("hardware.sensors.scale.enabled") && scale->isConnected();
}

/**
 * @brief The longest shot, also by hand: 60 s, or the target time + 10 s when that is longer. Without an
 *        over-pressure valve the pump should not run on for ever (Dominik, 08.10.2026).
 */
inline double brewMaxMs() {
    return std::max(kBrewMaxSeconds * 1000.0, totalTargetBrewTime + 10000.0);
}

/**
 * @brief The brew stopped by itself (time, weight, scale lost) and the brew switch is still on: the round display and
 *        the page keep the shot until it goes off (Dominik, 07.10.2026). Includes the one loop between the stop
 *        (kBrewFinished) and checkBrewSwitch() moving the switch on to kBrewSwitchWaitForRelease. A stop by hand
 *        never: the switch is already off. A backflush never: it does not set brewEndedSwitchOn.
 */
inline bool brewSwitchHeldAfterBrew() {
    return (currBrewState == kBrewFinished && currBrewSwitchState == kBrewSwitchShortPressed) ||
           (currBrewSwitchState == kBrewSwitchWaitForRelease && brewEndedSwitchOn);
}

/**
 * @brief The machine left kBrew while a brew was running: temperature control switched off on the page, sensor
 *        error, over temperature. Those states do not call brew(), so pump and valve would stay on (with a sensor
 *        error until the power plug is pulled; no OPV, and an empty tank runs the pump dry). Call from loop() after
 *        handleMachineState(). The next brew() logs the shot and goes idle.
 */
inline void brewSafetyStop() {
    if (checkBrewActive() && machineState != kBrew) {
        LOGF(WARNING, "Brew stopped: machine left the brew state (%d)", static_cast<int>(machineState));
        pumpRelay->off();
        valveRelay->off();
        currBrewState = kBrewFinished;
    }

    // the same for a backflush: valveSafetyShutdownCheck() closes the valve, but the pump would run on
    if (currBackflushState != kBackflushIdle && currBackflushState != kBackflushFinished && machineState != kBackflush) {
        LOGF(WARNING, "Backflush stopped: machine left the backflush state (%d)", static_cast<int>(machineState));
        pumpRelay->off();
        valveRelay->off();
        currBackflushState = kBackflushIdle;
        currBackflushCycles = 1;
        brewSwitchWasOff = false; // again only after the switch went off: not by itself when the machine is back
    }
}
#endif

/**
 * @brief Toggle or momentary input for Brew Switch
 */
inline void checkBrewSwitch() {
    if (!isPowerSwitchOperationAllowed()) {
        return;
    }

    static bool loggedEmptyWaterTank = false;
#ifdef CC_FAKE_TEMP_SENSOR
    brewSwitchReading = bench::brewSwitchOn() ? HIGH : LOW; // bench: GPIO34 floats without the PCB
#else
    brewSwitchReading = brewSwitch->isPressed();
#endif

    // Block brewSwitch input when water tank is empty
    if (machineState == kWaterTankEmpty) {

        if (!loggedEmptyWaterTank && (currBrewSwitchState == kBrewSwitchIdle || currBrewSwitchState == kBrewSwitchPressed)) {
            LOG(WARNING, "Brew switch input ignored: Water tank empty");
            loggedEmptyWaterTank = true;
        }
        return;
    }

    // Block brewSwitch input while hot water is being drawn
    if (machineState == kHotWater) {
        return;
    }

    loggedEmptyWaterTank = false;

    // Convert toggle brew switch input to brew switch state
    if (const int brewSwitchType = config.get<int>("hardware.switches.brew.type"); brewSwitchType == Switch::TOGGLE) {
        if (currReadingBrewSwitch != brewSwitchReading) {
            currReadingBrewSwitch = brewSwitchReading;
        }

        switch (currBrewSwitchState) {
            case kBrewSwitchIdle:
                if (currReadingBrewSwitch == HIGH) {
                    currBrewSwitchState = kBrewSwitchShortPressed;
                    LOG(DEBUG, "Toggle Brew switch is ON -> got to currBrewSwitchState = kBrewSwitchShortPressed");
                }
                break;

            case kBrewSwitchShortPressed:
                if (currReadingBrewSwitch == LOW) {
                    currBrewSwitchState = kBrewSwitchIdle;
                    LOG(DEBUG, "Toggle Brew switch is OFF -> got to currBrewSwitchState = kBrewSwitchIdle");
                }
                else if (currBrewState == kBrewFinished || currBackflushState == kBackflushFinished) {
                    currBrewSwitchState = kBrewSwitchWaitForRelease;
#ifdef CC_ORIONE
                    brewEndedSwitchOn = currBrewState == kBrewFinished;
#endif
                    LOG(DEBUG, "Brew reached target or backflush done -> got to currBrewSwitchState = kBrewSwitchWaitForRelease");
                }
                break;

            case kBrewSwitchWaitForRelease:
                if (currReadingBrewSwitch == LOW) {
                    currBrewSwitchState = kBrewSwitchIdle;
                    LOG(DEBUG, "Brew switch reset -> got to currBrewSwitchState = kBrewSwitchIdle");
                }
                break;

            default:

                currBrewSwitchState = kBrewSwitchIdle;
                LOG(DEBUG, "Unexpected switch state -> currBrewSwitchState = kBrewSwitchIdle");
                break;
        }
    }
    // Convert momentary brew switch input to brew switch state
    else if (brewSwitchType == Switch::MOMENTARY) {
        if (currReadingBrewSwitch != brewSwitchReading) {
            currReadingBrewSwitch = brewSwitchReading;
        }

        switch (currBrewSwitchState) {
            case kBrewSwitchIdle:
                if (currReadingBrewSwitch == HIGH) {
                    currBrewSwitchState = kBrewSwitchPressed;
                    LOG(DEBUG, "Brew switch press detected -> got to currBrewSwitchState = kBrewSwitchPressed");
                }
                break;

            case kBrewSwitchPressed:                // Brew switch pressed - check for short or long press
                if (currReadingBrewSwitch == LOW) { // Brew switch short press detected
                    currBrewSwitchState = kBrewSwitchShortPressed;
                    LOG(DEBUG, "Brew switch short press detected -> got to currBrewSwitchState = kBrewSwitchShortPressed; start brew");
                }
                else if (currReadingBrewSwitch == HIGH && brewSwitch->longPressDetected()) { // Brew switch long press detected
                    currBrewSwitchState = kBrewSwitchLongPressed;
                    LOG(DEBUG, "Brew switch long press detected -> got to currBrewSwitchState = kBrewSwitchLongPressed; start manual flush");
                }
                break;

            case kBrewSwitchShortPressed:
                if (currReadingBrewSwitch == HIGH) { // Brew switch short press detected while brew is running - abort brew
                    currBrewSwitchState = kBrewSwitchWaitForRelease;
                    LOG(DEBUG, "Brew switch short press detected -> got to currBrewSwitchState = kBrewSwitchWaitForRelease; brew or backflush stopped manually");
                }
                else if (currBrewState == kBrewFinished || currBackflushState == kBackflushFinished) { // Brew reached target and stopped or blackflush cycle done
                    currBrewSwitchState = kBrewSwitchWaitForRelease;
                    LOG(DEBUG, "Brew reached target or backflush done -> got to currBrewSwitchState = kBrewSwitchWaitForRelease");
                }
                break;

            case kBrewSwitchLongPressed:
                if (currReadingBrewSwitch == LOW) { // Brew switch got released after long press detected - reset brewswitch
                    currBrewSwitchState = kBrewSwitchWaitForRelease;
                    LOG(DEBUG, "Brew switch long press released -> got to currBrewSwitchState = kBrewSwitchWaitForRelease; stop manual flush");
                }
                break;

            case kBrewSwitchWaitForRelease: // wait for brew switch got released
                if (currReadingBrewSwitch == LOW) {
                    currBrewSwitchState = kBrewSwitchIdle;
                    LOG(DEBUG, "Brew switch reset -> got to currBrewSwitchState = kBrewSwitchIdle");
                }
                break;

            default:
                currBrewSwitchState = kBrewSwitchIdle;
                LOG(DEBUG, "Unexpected switch state -> currBrewSwitchState = kBrewSwitchIdle");
                break;
        }
    }
}

/**
 * @brief If set to publish debug messages then list what the current action is and what triggered it
 * @return void
 */
inline void debugPumpState(String label, String state) {
    hotWaterStateDebug = state;
    if (hotWaterStateDebug != lastHotWaterStateDebug) {
        LOGF(DEBUG, "Hot water state: %s - BrewHandler: %s", hotWaterStateDebug, label);
        lastHotWaterStateDebug = hotWaterStateDebug;
    }
}

/**
 * @brief Brew process handeling including timer and state machine for brew-by-time and brew-by-weight
 * @return true if brew is running, false otherwise
 */
inline bool brew() {
    if (!config.get<bool>("hardware.switches.brew.enabled") || brewSwitch == nullptr) {
        return false; // brew switch is not enabled, so no brew process running
    }

    const unsigned long currentMillisTemp = millis();
    checkBrewSwitch();

    // abort function for state machine from every state
    if (currBrewSwitchState == kBrewSwitchIdle && currBrewState > kBrewIdle && currBrewState < kBrewFinished) {
        if (currBrewState != kBrewFinished) {
            LOG(INFO, "Brew stopped manually");
        }
        currBrewState = kBrewFinished;
    }
    // calculated brew time while brew is running
    if (currBrewState > kBrewIdle && currBrewState < kBrewFinished) {
        currBrewTime = currentMillisTemp - startingTime;
    }

    const int brewMode = config.get<int>("brew.mode");
    const bool brewByTimeEnabled = brewMode != 0 && config.get<bool>("brew.by_time.enabled");
    const bool brewByWeightEnabled = brewMode != 0 && config.get<bool>("brew.by_weight.enabled");
#ifdef CC_ORIONE
    bool preinfusionEnabled = config.get<bool>("brew.pre_infusion.enabled") && !rinseRun; // the rinse after a shot: none
#else
    const bool preinfusionEnabled = config.get<bool>("brew.pre_infusion.enabled");
#endif

    // check if brewswitch was turned off after a brew; Brew only runs once even brewswitch is still pressed
    if (currBrewSwitchState == kBrewSwitchIdle) {
        brewSwitchWasOff = true;
    }

    // set brew time every cycle, in case changes are done during brew
    if (targetBrewTime > 0) {
        totalTargetBrewTime = targetBrewTime * 1000;

        if (preinfusionEnabled) {
            totalTargetBrewTime += preinfusion * 1000 + preinfusionPause * 1000;
        }
    }
    else {
        // Stop by time deactivated --> totalTargetBrewTime = 0
        totalTargetBrewTime = 0;
    }

    // state machine for brew
    switch (currBrewState) {
        case kBrewIdle:             // waiting step for brew switch turning on
            if (currBrewSwitchState == kBrewSwitchShortPressed && brewSwitchWasOff && !backflushOn && machineState != kBackflush) {
                startingTime = millis();
                currBrewTime = 0;   // reset currBrewTime, last brew is still stored
                currBrewWeight = 0; // reset currBrewWeight for new brew

                LOG(INFO, "Brew started");
#ifdef CC_ORIONE
                brewStoppedByWeight = false;
                // by weight without the scale (off, not chosen): the target time ends this shot (Dominik, 08.10.2026)
                brewWeightFallback = brewByWeightEnabled && !brewScaleReady();

                if (brewWeightFallback) {
                    LOGF(WARNING, "Brew by weight without the scale: stops at the target time (%.0f s)", totalTargetBrewTime / 1000);
                }
#endif
#ifdef CC_ORIONE
                rinseRun = shot_history::rinseExpected();
                preinfusionEnabled = config.get<bool>("brew.pre_infusion.enabled") && !rinseRun;

                if (rinseRun && config.get<bool>("brew.pre_infusion.enabled")) {
                    LOG(INFO, "Rinse after the shot: no pre-infusion");
                }

                shot_history::brewStarted(temperature, preinfusionEnabled);
#endif

                if (!preinfusionEnabled) {
                    LOG(INFO, "Brew running");
                    currBrewState = kBrewRunning;
                }
                else if (preinfusion == 0) {
                    LOG(INFO, "Preinfusion was zero, Preinfusion pause running");
                    currBrewState = kPreinfusionPause;
                }
                else {
                    LOG(INFO, "Preinfusion running");
                    currBrewState = kPreinfusion;
                }

                if (scale && config.get<bool>("hardware.sensors.scale.enabled") && config.get<int>("hardware.sensors.scale.type") == 2) {
                    const auto bleScale = static_cast<BluetoothScale*>(scale);

                    if (config.get<bool>("display.blescale_brew_timer")) {
                        bleScale->resetTimer();
                        bleScale->startTimer();
                    }

                    if (config.get<bool>("brew.by_weight.enabled") && config.get<bool>("brew.by_weight.auto_tare")) {
                        // only send tare command if not already close to zero
                        if (abs(currReadingWeight) > 0.2) {
                            LOG(INFO, "Tare scale");
                            bleScale->tare();
                            // Mark that auto-tare is in progress for Bluetooth scales
                            autoTareInProgress = true;
                            autoTareStartTime = millis();
                        }
                    }
                }
            }

            break;

        case kPreinfusion:
            valveRelay->on();
            pumpRelay->on();
            debugPumpState("Preinfusion", "on");

            if (currBrewTime > preinfusion * 1000) {
                LOG(INFO, "Preinfusion pause running");
                currBrewState = kPreinfusionPause;
            }

            break;

        case kPreinfusionPause:
            // Orione: the open valve also powers the Pulsor board, which then pulses the pump (orione-full-build.md 6.2): a
            // slow trickle instead of a pause. Closing the valve instead vented the group: on 08.10.2026 that threw ~17 g of
            // the burst onto the scale, not into the cup, and the shot started again from an empty group
            valveRelay->on();
            pumpRelay->off();
            debugPumpState("Pause", "off");

            if (currBrewTime > (preinfusion + preinfusionPause) * 1000) {
                LOG(INFO, "Brew running");
                currBrewState = kBrewRunning;
            }

            break;

        case kBrewRunning:
            {
                valveRelay->on();
                pumpRelay->on();
                debugPumpState("BrewRunning", "on");

#ifdef CC_ORIONE
                if (brewByWeightEnabled && !brewWeightFallback && !brewScaleReady()) {
                    LOGF(WARNING, "Scale lost during the shot: stops at the target time (%.0f s)", totalTargetBrewTime / 1000);
                    brewWeightFallback = true; // for the rest of this shot, also if it comes back: its weight missed a part
                }

                if (currBrewTime > totalTargetBrewTime && totalTargetBrewTime > 0 && (brewByTimeEnabled || brewWeightFallback)) {
                    LOG(INFO, "Brew reached time target");
                    currBrewState = kBrewFinished;
                }
                else if (currBrewTime >= brewMaxMs()) {
                    LOGF(WARNING, "Brew reached the longest shot (%.0f s)", brewMaxMs() / 1000);
                    currBrewState = kBrewFinished;
                }
                else if (brewByWeightEnabled && !brewWeightFallback) {
#else
                if (currBrewTime > totalTargetBrewTime && brewByTimeEnabled) {
                    LOG(INFO, "Brew reached time target");
                    currBrewState = kBrewFinished;
                }
                else if (scale && config.get<bool>("hardware.sensors.scale.enabled")) {
#endif
                    const auto targetBrewWeight = ParameterRegistry::getInstance().getParameterById("brew.by_weight.target_weight")->getValueAs<float>();

#ifdef CC_ORIONE
                    // a lead before the target: the scale reports late and drops follow, as many as run in the
                    // learned lag at the flow right now (orione::BrewLag, learned in shotHistory.h)
                    const float lag = static_cast<float>(config.get<double>("brew.by_weight.lag"));
                    const float flow = shot_history::flowMeter.running() ? shot_history::flowMeter.flow() : 0.0f;
                    const float stopAt = targetBrewWeight - orione::BrewLag::leadGrams(lag, flow);
#else
                    const float stopAt = targetBrewWeight;
#endif

                    if (currBrewWeight > stopAt && brewByWeightEnabled) {
                        LOG(INFO, "Brew reached weight target");
#ifdef CC_ORIONE
                        brewStoppedByWeight = true;
                        shot_history::noteStop(lag, flow);
#endif
                        currBrewState = kBrewFinished;
                    }
                }

                break;
            }

        case kBrewFinished:
            {
                valveRelay->off();
                pumpRelay->off();
                debugPumpState("BrewFinished", "off");

                brewSwitchWasOff = false;
                LOG(INFO, "Brew finished");
                LOGF(INFO, "Shot time: %4.1f s", currBrewTime / 1000);
#ifdef CC_ORIONE
                shot_history::brewEnded(currBrewTime / 1000, scale && config.get<bool>("hardware.sensors.scale.enabled") && scale->isConnected() ? std::max(0.0f, static_cast<float>(currBrewWeight)) : -1.0f, brewStoppedByWeight);
#endif
                LOG(INFO, "Brew idle");
                currBrewState = kBrewIdle;

                if (scale && config.get<bool>("hardware.sensors.scale.enabled") && config.get<int>("hardware.sensors.scale.type") == 2 && config.get<bool>("display.blescale_brew_timer")) {
                    static_cast<BluetoothScale*>(scale)->stopTimer();
                }

                break;
            }

        default:
            currBrewState = kBrewIdle;
            LOG(DEBUG, "Unexpected brew state -> currBrewState = kBrewIdle");

            break;
    }

    return checkBrewActive();
}

/**
 * @brief manual grouphead flush
 * @return true if manual flush is running, false otherwise
 */
inline bool manualFlush() {
    if (!config.get<bool>("hardware.switches.brew.enabled") || brewSwitch == nullptr) {
        return false; // brew switch is not enabled, so no brew process running
    }

    const unsigned long currentMillisTemp = millis();
    checkBrewSwitch();

    if (currManualFlushState == kManualFlushRunning) {
        currBrewTime = currentMillisTemp - startingTime;
    }

    switch (currManualFlushState) {
        case kManualFlushIdle:
            if (currBrewSwitchState == kBrewSwitchLongPressed) {
                startingTime = millis();
                valveRelay->on();
                pumpRelay->on();
                debugPumpState("ManualFlush", "on");
                LOG(INFO, "Manual flush started");
                currManualFlushState = kManualFlushRunning;
            }
            break;

        case kManualFlushRunning:
            if (currBrewSwitchState != kBrewSwitchLongPressed) {
                valveRelay->off();
                pumpRelay->off();
                debugPumpState("ManualFlush", "off");
                LOG(INFO, "Manual flush stopped");
                LOGF(INFO, "Manual flush time: %4.1f s", currBrewTime / 1000);
                currManualFlushState = kManualFlushIdle;
            }
            break;

        default:
            currManualFlushState = kManualFlushIdle;
            LOG(DEBUG, "Unexpected manual flush state -> currManualFlushState = kManualFlushIdle");

            break;
    }

    return currManualFlushState == kManualFlushRunning;
}

/**
 * @brief Backflush
 */
inline void backflush() {
    if (!config.get<bool>("hardware.switches.brew.enabled") || brewSwitch == nullptr) {
        return; // brew switch is not enabled, so no brew process running
    }

    checkBrewSwitch();

    if (currBackflushState != kBackflushIdle && !backflushOn) {
        currBackflushState = kBackflushFinished; // Force reset in case backflushOn is reset during backflush!
        LOG(INFO, "Backflush: Disabled via webinterface");
    }
    else if (offlineMode || currBrewState > kBrewIdle || backflushCycles <= 0 || !backflushOn) {
        return;
    }

    // abort function for state machine from every state
    if (currBrewSwitchState == kBrewSwitchIdle && currBackflushState > kBackflushIdle && currBackflushState < kBackflushFinished) {
        currBackflushState = kBackflushFinished;

        LOG(INFO, "Backflush stopped manually");
    }

    // check if brewswitch was turned off after a backflush; Backflush only runs once even brewswitch is still pressed
    if (currBrewSwitchState == kBrewSwitchIdle) {
        brewSwitchWasOff = true;
    }

    // State machine for backflush
    switch (currBackflushState) {
        case kBackflushIdle:
            if (currBrewSwitchState == kBrewSwitchShortPressed && backflushOn && brewSwitchWasOff) {
                startingTime = millis();
                valveRelay->on();
                pumpRelay->on();
                debugPumpState("Backflush", "on");
                LOGF(INFO, "Start backflush cycle %d", currBackflushCycles);
                LOG(INFO, "Backflush: filling portafilter");
                currBackflushState = kBackflushFilling;
#ifdef CC_ORIONE
                care::cleaning.cyclesStart(); // after the detergent and the rinsed-out basket: the cycles with clear water
#endif
            }

            break;

        case kBackflushFilling:
            if (millis() - startingTime > backflushFillTime * 1000) {
                startingTime = millis();
                valveRelay->off();
                pumpRelay->off();
                debugPumpState("Backflush", "off");
                LOG(INFO, "Backflush: flushing into drip tray");

                if (currBackflushCycles == backflushCycles) {
                    currBackflushState = kBackflushEnding;
                }
                else {
                    currBackflushState = kBackflushFlushing;
                }
            }

            break;

        case kBackflushFlushing:
            if (millis() - startingTime > backflushFlushTime * 1000) {
                if (currBackflushCycles < backflushCycles) {
                    startingTime = millis();
                    valveRelay->on();
                    pumpRelay->on();
                    debugPumpState("Backflush", "on");
                    currBackflushCycles++;
                    LOGF(INFO, "Backflush: next backflush cycle %d", currBackflushCycles);
                    LOG(INFO, "Backflush: filling portafilter");
                    currBackflushState = kBackflushFilling;
                }
                else {
                    currBackflushState = kBackflushFinished;
                }
            }

            break;

        case kBackflushEnding:
            if (millis() - startingTime > backflushFlushTime * 1000) {
                currBackflushState = kBackflushFinished;
#ifdef CC_ORIONE
                care::backflushed(backflushCycles, static_cast<float>(backflushFillTime));

                // with detergent (care::cleaning): the first round leaves backflush mode on, the basket is rinsed out
                // and the brew switch starts the same cycles with clear water
                if (care::cleaning.phase() == orione::CleaningProgram::kOff || care::cleaning.cyclesDone()) {
                    shot_history::backflushDone(); // all cycles run: the reminder starts counting again
                    backflushCompleted = true;
                }
                else {
                    LOG(INFO, "Cleaning: detergent cycles done, rinse the basket out, then the brew switch off and on again");
                }
#endif
            }

            break;

        case kBackflushFinished:
            valveRelay->off();
            pumpRelay->off();
            debugPumpState("Backflush", "off");
            LOGF(INFO, "Backflush finished after %d cycles", currBackflushCycles);
            currBackflushCycles = 1;
            brewSwitchWasOff = false;
            currBackflushState = kBackflushIdle;
#ifdef CC_ORIONE
            // done: backflush mode goes off, the next shot is coffee again (Dominik, 08.10.2026); stopped by hand
            // before the end it stays on, to start again
            if (backflushCompleted) {
                backflushCompleted = false;
                backflushOn = false;
                // nothing else happens until the brew switch goes off: say so (Dominik, 08.10.2026: "nach backflush mit
                // bezugsschalter noch an gibt es keine meldung")
                backflushSwitchReminder = currBrewSwitchState != kBrewSwitchIdle;
                LOG(INFO, "Backflush mode off");
            }
#endif

            break;

        default:
            currBackflushState = kBackflushIdle;
            LOG(DEBUG, "Unexpected backflush state -> currBackflushState = kBackflushIdle");

            break;
    }
}
