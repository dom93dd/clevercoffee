/**
 * @file FakeMachine.h
 *
 * @brief Very small stand-in for the espresso machine, only good enough to drive the display:
 *        thermoblock heating with a PI controller, brew by time / weight, flush, hot water,
 *        backflush, steam and the error states. Loosely modelled on a Quick Mill Orione 3000.
 */

#pragma once

#include <RoundDisplayControl.h>
#include <RoundDisplayModel.h>

#include <algorithm>
#include <cmath>

class FakeMachine {
    public:
        // Settings (as in the web UI)
        float brewSetpoint = 94.0f;
        float steamSetpoint = 125.0f;
        float targetBrewTime = 25.0f;   // 0 = off
        float targetBrewWeight = 36.0f; // used when the scale is on
        float postBrewHold = 3.0f;      // display.post_brew_timer_duration
        int backflushCycles = 5;
        bool scale = false;
        bool bleScale = true;
        bool scaleConnected = true;
        bool scaleBroken = false;
        bool warmupFlushPending = false; // cold start, sensor and setting on: it will flush by itself
        rd::Language language = rd::Language::German;

        // Environment
        bool wifiConnected = true;
        bool offline = false;
        int wifiBars = 3;

        void reset(const float startTemperature = 22.0f) {
            block_ = sensor_ = startTemperature;
            integral_ = 0;
            brewSwitch_ = false;
            brewing_ = false;
            brewTime_ = 0;
            holdLeft_ = 0;
            phase_ = rd::BrewPhase::Idle;
            weight_ = 0;
            lastShot_ = 0;
            steam_ = false;
            flush_ = false;
            hotWater_ = false;
            backflush_ = false;
            waterEmpty_ = false;
            sensorError_ = false;
            emergency_ = false;
            pidEnabled_ = true;
            standby_ = false;
        }

        // Controls -------------------------------------------------------------------------

        void toggleBrewSwitch() {
            brewSwitch_ = !brewSwitch_;

            if (backflush_) {
                if (brewSwitch_ && backflushPhase_ == rd::BackflushPhase::Idle) {
                    backflushPhase_ = rd::BackflushPhase::Filling;
                    backflushCycle_ = 1;
                    backflushTimer_ = 0;
                }
                else if (!brewSwitch_ && backflushPhase_ == rd::BackflushPhase::Ending) {
                    backflush_ = false;
                    backflushPhase_ = rd::BackflushPhase::Idle;
                }
                return;
            }

            if (brewSwitch_ && canBrew()) {
                brewing_ = true;
                brewTime_ = 0;
                weight_ = 0;
                phase_ = rd::BrewPhase::Running;
            }
            else if (!brewSwitch_) {
                if (brewing_) {
                    finishBrew();
                }
                else if (phase_ == rd::BrewPhase::Finished) {
                    holdLeft_ = postBrewHold; // switched off after a shot that stopped by itself
                }
                heldFor_ = 0;
                phase_ = rd::BrewPhase::Idle;
            }
        }

        void toggleSteam() {
            steam_ = !steam_;
        }

        void toggleFlush() {
            flush_ = !flush_ && canBrew();
            flushTime_ = 0;
        }

        void toggleHotWater() {
            hotWater_ = !hotWater_ && canBrew();
            hotWaterTime_ = 0;
        }

        void toggleBackflush() {
            backflush_ = !backflush_;
            backflushPhase_ = rd::BackflushPhase::Idle;
            backflushCycle_ = 0;
        }

        void toggleWaterEmpty() {
            waterEmpty_ = !waterEmpty_;
        }

        void toggleSensorError() {
            sensorError_ = !sensorError_;
        }

        void triggerOvertemperature() {
            block_ = sensor_ = 146.0f;
            emergency_ = true;
        }

        void togglePid() {
            pidEnabled_ = !pidEnabled_;
        }

        void toggleStandby() {
            standby_ = !standby_;
        }

        void nudgeTemperature(const float delta) {
            block_ += delta;
            sensor_ += delta;
        }

        /** Jump to a steady state at the setpoint (for screenshots). */
        void settle() {
            block_ = sensor_ = currentSetpoint();
            integral_ = kLoss * (block_ - kAmbient) / kHeaterWatts * 100.0f / kKi;
        }

        // Simulation -----------------------------------------------------------------------

        void step(const float dt) {
            const float setpoint = currentSetpoint();

            // Emergency stop: heater off until below brew setpoint + 5 K (as in the firmware)
            if (emergency_ && sensor_ < brewSetpoint + 5.0f) {
                emergency_ = false;
            }

            const bool heaterAllowed = pidEnabled_ && !standby_ && !waterEmpty_ && !sensorError_ && !emergency_ && !backflush_;
            const float error = setpoint - sensor_;

            if (heaterAllowed) {
                // PI controller with clamping anti-windup
                const float p = kKp * error;
                const float next = integral_ + error * dt;
                const float unclamped = p + kKi * next;

                if (unclamped > 0.0f && unclamped < 100.0f) {
                    integral_ = next;
                }

                heater_ = std::clamp(p + kKi * integral_, 0.0f, 100.0f);
            }
            else {
                heater_ = 0;
            }

            // Thermoblock: heater power, losses to the room, water flowing through
            const bool pumping = brewing_ || flush_ || hotWater_ || (backflush_ && backflushPhase_ == rd::BackflushPhase::Filling);
            const float flowCooling = pumping ? kFlow * 4.18f * (block_ - kAmbient) : 0.0f;
            const float power = heater_ / 100.0f * kHeaterWatts - kLoss * (block_ - kAmbient) - flowCooling;
            block_ += power / kHeatCapacity * dt;

            // The sensor follows the block with a delay
            sensor_ += (block_ - sensor_) * std::min(1.0f, dt / kSensorLag);

            if (brewing_) {
                brewTime_ += dt;

                if (scale && brewTime_ > 6.0f) {
                    weight_ += std::min(1.0f, (brewTime_ - 6.0f) / 3.0f) * 1.9f * dt;
                }

                const bool byTime = targetBrewTime > 0.0f && brewTime_ >= targetBrewTime;
                const bool byWeight = scale && targetBrewWeight > 0.0f && weight_ >= targetBrewWeight;

                if (byTime || byWeight) {
                    finishBrew();
                    phase_ = rd::BrewPhase::Finished; // switch still on
                }
            }
            else if (holdLeft_ > 0.0f) {
                // Done with the switch still on: the result stays until it is switched off (as the firmware)
                if (brewSwitch_ && phase_ == rd::BrewPhase::Finished) {
                    heldFor_ += dt;
                }
                else {
                    holdLeft_ -= dt;
                }

                // Drips after the pump stopped
                if (scale && dripLeft_ > 0.0f) {
                    dripLeft_ -= dt;
                    weight_ += 0.4f * dt;
                }
            }

            if (flush_) {
                flushTime_ += dt;
            }

            if (hotWater_) {
                hotWaterTime_ += dt;
            }

            if (backflush_ && (backflushPhase_ == rd::BackflushPhase::Filling || backflushPhase_ == rd::BackflushPhase::Flushing)) {
                backflushTimer_ += dt;

                if (backflushPhase_ == rd::BackflushPhase::Filling && backflushTimer_ > 5.0f) {
                    backflushPhase_ = rd::BackflushPhase::Flushing;
                    backflushTimer_ = 0;
                }
                else if (backflushPhase_ == rd::BackflushPhase::Flushing && backflushTimer_ > 10.0f) {
                    backflushTimer_ = 0;

                    if (backflushCycle_ >= backflushCycles) {
                        backflushPhase_ = rd::BackflushPhase::Ending;
                    }
                    else {
                        ++backflushCycle_;
                        backflushPhase_ = rd::BackflushPhase::Filling;
                    }
                }
            }
        }

        rd::Model model() const {
            rd::Model m;
            m.language = language;
            m.mode = mode();
            m.temperature = sensorError_ ? -49.9f : sensor_;
            m.setpoint = currentSetpoint();
            m.heaterPercent = heater_;
            m.readyBand = 0.3f;
            m.emergencyResetTemp = brewSetpoint + 5.0f;

            m.brewTimerVisible = brewing_ || holdLeft_ > 0.0f;
            m.warmupFlushPending = warmupFlushPending;
            m.brewSwitchReminder = !brewing_ && brewSwitch_ && phase_ == rd::BrewPhase::Finished && heldFor_ >= rd::BrewTimer::kRemindMs / 1000.0f;
            m.brewPhase = phase_;
            m.brewTime = brewTime_;
            m.brewTargetTime = targetBrewTime;
            m.lastBrewTime = lastShot_;
            m.flushTime = flushTime_;
            m.hotWaterTime = hotWaterTime_;

            m.scaleEnabled = scale;
            m.bleScale = scale && bleScale;
            m.bleScaleConnected = scale && scaleConnected;
            m.scaleFault = scale && scaleBroken;
            m.brewWeight = weight_;
            m.brewTargetWeight = scale ? targetBrewWeight : 0.0f;

            m.backflushPhase = backflushPhase_;
            m.backflushCycle = static_cast<uint8_t>(backflushCycle_);
            m.backflushCycles = static_cast<uint8_t>(backflushCycles);

            m.offlineMode = offline;
            m.wifiConnected = wifiConnected && !offline;
            m.wifiBars = static_cast<uint8_t>(wifiBars);
            return m;
        }

        rd::Mode mode() const {
            // Same priorities as handleMachineState() in main.cpp
            if (emergency_) return rd::Mode::EmergencyStop;
            if (sensorError_) return rd::Mode::SensorError;
            if (standby_) return rd::Mode::Standby;
            if (!pidEnabled_) return rd::Mode::PidDisabled;
            if (waterEmpty_) return rd::Mode::WaterTankEmpty;
            if (backflush_) return rd::Mode::Backflush;
            if (brewing_) return rd::Mode::Brew;
            if (flush_) return rd::Mode::ManualFlush;
            if (hotWater_) return rd::Mode::HotWater;
            if (steam_) return rd::Mode::Steam;
            return rd::Mode::Normal;
        }

        float sensorTemperature() const {
            return sensor_;
        }

        float heater() const {
            return heater_;
        }

        bool brewSwitch() const {
            return brewSwitch_;
        }

        // Direct state setters for screenshots
        void setBrewing(const float seconds, const float weight) {
            brewing_ = true;
            brewSwitch_ = true;
            brewTime_ = seconds;
            weight_ = weight;
            phase_ = rd::BrewPhase::Running;
        }

        void setBrewDone(const float seconds, const float weight) {
            brewing_ = false;
            brewTime_ = seconds;
            lastShot_ = seconds;
            weight_ = weight;
            holdLeft_ = postBrewHold;
            phase_ = rd::BrewPhase::Finished;
        }

        void setLastShot(const float seconds) {
            lastShot_ = seconds;
        }

        void setFlush(const float seconds) {
            flush_ = true;
            flushTime_ = seconds;
        }

        void setHotWater(const float seconds) {
            hotWater_ = true;
            hotWaterTime_ = seconds;
        }

        void setBackflush(const rd::BackflushPhase phase, const int cycle) {
            backflush_ = true;
            backflushPhase_ = phase;
            backflushCycle_ = cycle;
        }

        void setHeater(const float percent) {
            heater_ = percent;
        }

    private:
        static constexpr float kAmbient = 21.0f;
        static constexpr float kHeaterWatts = 1100.0f;
        static constexpr float kHeatCapacity = 900.0f; // J/K
        static constexpr float kLoss = 3.5f;           // W/K
        static constexpr float kFlow = 2.2f;           // g/s during a shot
        static constexpr float kSensorLag = 3.0f;      // s
        static constexpr float kKp = 9.0f;
        static constexpr float kKi = 0.12f;

        float currentSetpoint() const {
            return steam_ ? steamSetpoint : brewSetpoint;
        }

        bool canBrew() const {
            return !waterEmpty_ && !sensorError_ && !emergency_ && !standby_;
        }

        void finishBrew() {
            brewing_ = false;
            lastShot_ = brewTime_;
            holdLeft_ = postBrewHold;
            dripLeft_ = 1.5f;
            heldFor_ = 0;
        }

        float block_ = 22.0f;
        float sensor_ = 22.0f;
        float heater_ = 0;
        float integral_ = 0;

        bool brewSwitch_ = false;
        bool brewing_ = false;
        float brewTime_ = 0;
        float holdLeft_ = 0;
        float dripLeft_ = 0; // drops still coming after the pump stopped
        float heldFor_ = 0;  // done, switch still on: for how long
        rd::BrewPhase phase_ = rd::BrewPhase::Idle;
        float weight_ = 0;
        float lastShot_ = 0;

        bool steam_ = false;
        bool flush_ = false;
        float flushTime_ = 0;
        bool hotWater_ = false;
        float hotWaterTime_ = 0;
        bool backflush_ = false;
        rd::BackflushPhase backflushPhase_ = rd::BackflushPhase::Idle;
        int backflushCycle_ = 0;
        float backflushTimer_ = 0;

        bool waterEmpty_ = false;
        bool sensorError_ = false;
        bool emergency_ = false;
        bool pidEnabled_ = true;
        bool standby_ = false;
};
