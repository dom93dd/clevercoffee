/**
 * @file RoundDisplayModel.h
 *
 * @brief Snapshot of the machine state that the round display shows.
 *
 * The UI only reads this struct. The firmware fills it from its globals,
 * the desktop simulator fills it from a simulated machine. No Arduino
 * dependencies on purpose.
 */

#pragma once

#include <cstdint>

namespace rd {

    enum class Mode : uint8_t {
        Init,
        Normal,
        Brew,
        ManualFlush,
        Steam,
        HotWater,
        Backflush,
        PidDisabled,
        WaterTankEmpty,
        Standby,
        EmergencyStop,
        SensorError,
    };

    enum class BrewPhase : uint8_t {
        Idle,
        Preinfusion,
        PreinfusionPause,
        Running,
        Finished,
    };

    enum class BackflushPhase : uint8_t {
        Idle,
        Filling,
        Flushing,
        Ending,
        Finished,
    };

    enum class Language : uint8_t {
        German,
        English,
    };

    struct Model {
            Mode mode = Mode::Init;
            Language language = Language::German;

            // Temperatures in degC
            float temperature = 0;
            float setpoint = 0;
            float heaterPercent = 0;      // PID output 0..100
            float readyBand = 0.3f;       // |temperature - setpoint| within this counts as ready (display.blinking.delta)
            float emergencyResetTemp = 0; // heater is released again below this temperature

            // Timers in seconds. brewTimerVisible also covers the hold time after a shot.
            bool brewTimerVisible = false;
            BrewPhase brewPhase = BrewPhase::Idle;
            float brewTime = 0;
            float brewTargetTime = 0; // 0 = no brew by time
            float lastBrewTime = 0;   // duration of the previous shot, 0 = none yet
            bool brewSwitchReminder = false; // the shot is done, the brew switch has been left on for a while
            bool warmupFlushPending = false; // after a cold start the machine will flush by itself once settled
            bool flushReminder = false;      // a shot since the last rinse: rinse the shower screen (Orione)
            bool backflushDone = false;      // backflush over, brew switch still on: switch it off (Orione)
            bool switchWakes = false;        // standby: the brew switch wakes the machine (Orione)
            int16_t clockMinutes = -1;       // standby: local time of day in minutes, -1 = no clock (no NTP yet)
            bool standbyWarm = false;        // standby: the block is kept warm (Orione), "warm" before the temperature
            // descaling program (Orione, OrioneDescale.h): phase 0 off, 1 cooling, 2 rounds, 3 more solution, 4 rest
            // through, 5 clear water then "Weiter", 6 rinsing, 7 done
            uint8_t descalePhase = 0;
            uint8_t descaleRound = 0;      // 1..descaleRounds in the rounds
            uint8_t descaleRounds = 8;
            uint8_t descalePass = 0;       // rinse pass 1..2
            int16_t descaleSecondsLeft = -1; // of pumping or soaking in the rounds
            bool descalePumping = false;
            bool steamByThermostat = false;  // steam screen: the original steam thermostat heats, the firmware only watches
            bool steamCooling = false;       // after steam: too hot for espresso until it has cooled down
            float flushTime = 0;
            float flushTargetTime = 0; // a flush that ends by itself after this many seconds: one revolution of the ring (Orione: the rinse after a shot, 10 s)
            float hotWaterTime = 0;

            // Scale
            bool scaleEnabled = false;
            bool scaleFault = false;
            bool bleScale = false;
            bool bleScaleConnected = false;
            float brewWeight = 0;
            float brewTargetWeight = 0; // 0 = no brew by weight

            // Backflush
            BackflushPhase backflushPhase = BackflushPhase::Idle;
            uint8_t backflushCycle = 0;
            uint8_t backflushCycles = 0;
            uint8_t cleaningPhase = 0; // cleaning with detergent (Orione): 1 detergent cycles, 2 rinse the basket out, 3 clear water

            // Connectivity
            bool offlineMode = false;
            bool wifiConnected = false;
            uint8_t wifiBars = 0; // 0..4
            bool mqttEnabled = false;
            bool mqttConnected = false;
    };

    /**
     * @brief Full screen message for boot, WiFi setup and scale calibration,
     *        e.g. {"IP-ADRESSE", "silvia.local", "192.168.178.42"}.
     *        The title uses the capitals font; lower case letters are shown in the text font instead.
     *        With qr set (e.g. "WIFI:T:WPA;S:orione;P:...;;" to join the setup WiFi with the phone
     *        camera) the screen shows the title, the code and line1/line2 below it.
     */
    struct Message {
            const char* title = nullptr;
            const char* line1 = nullptr;
            const char* line2 = nullptr;
            const char* line3 = nullptr;
            const char* qr = nullptr;
    };

} // namespace rd
