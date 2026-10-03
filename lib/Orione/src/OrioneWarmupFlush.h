/**
 * @file OrioneWarmupFlush.h
 *
 * @brief Warm-up flush: three short pulses of valve and pump with pauses, which bring the hose to the
 *        group and the portafilter up to temperature on the Orione's thermoblock. Runs by itself once
 *        after a cold start, when the brew temperature has settled near the setpoint, or when started by
 *        hand (web page, Wartung). Only with a water level sensor (Dominik, 03.10.2026): without one,
 *        an empty tank would run the pump dry. Why pulses and not 20 s in one go: the block heats
 *        ~3 ml/s, a flush moves ~10 ml/s (research/startup-flush-thermoblock.md in the project folder).
 *
 * Anything else ends it for good until the next cold start: the brew switch, a brew, backflush,
 * standby, an error, an empty tank. No Arduino dependencies: tested in simulator/test/test_orione.
 */

#pragma once

#include <cmath>
#include <cstdint>

namespace orione {

    class WarmupFlush {
        public:
            static constexpr float kColdBelow = 50.0f;     // first reading below: a cold start
            // Settled: within kNear of the setpoint and moving less than kSpread for kSettledMs. Not "at
            // the setpoint": a controller that holds a steady offset (untuned PID; the bench's simulated
            // block sits at 90.2 for 93) would never let it run.
            static constexpr double kNear = 5.0;
            static constexpr double kSpread = 1.5;
            static constexpr uint32_t kSettledMs = 120000; // that long settled before the first pulse
            static constexpr uint32_t kPulseMs = 3000;
            static constexpr uint32_t kPauseMs = 20000;
            static constexpr int kPulses = 3;

            enum Phase : uint8_t {
                kNone = 0,    // no cold start (or no reading yet): nothing pending
                kWaiting = 1, // cold start: waits for the temperature to settle
                kPulse = 2,   // valve and pump on
                kPause = 3,   // between pulses
                kDone = 4,    // ran, was cut short or the user took over: not again until the next cold start
            };

            struct Inputs {
                    bool sensor;     // water level sensor enabled: without it nothing runs
                    bool automatic;  // setting: flush by itself after a cold start
                    bool tankOk;     // the sensor sees water
                    bool ready;      // nothing else going on (machine state normal, or this flush)
                    bool userActive; // brew switch on, brewing, backflush, hot water, steam
            };

            /** @return true while valve and pump should run. Call every loop. */
            bool update(const uint32_t nowMs, const double celsius, const double setpoint, const Inputs& in) {
                if (!seen_) {
                    if (!std::isfinite(celsius) || celsius <= 0.0) {
                        return false; // no reading yet (or the TSIC's -49.9 error value)
                    }

                    seen_ = true;
                    phase_ = celsius < kColdBelow ? kWaiting : kNone;
                }

                if (startWanted_) {
                    startWanted_ = false;

                    if (!running() && in.sensor && in.tankOk && in.ready && !in.userActive) {
                        begin(nowMs);
                    }
                }

                switch (phase_) {
                    case kWaiting:
                        if (in.userActive) {
                            phase_ = kDone; // the user took over
                            return false;
                        }

                        if (!in.sensor || !in.automatic || !in.tankOk || !in.ready || std::fabs(celsius - setpoint) > kNear) {
                            settledSince_ = 0;
                            return false;
                        }

                        // a new run of readings when it starts or moves too much
                        if (settledSince_ == 0 || std::fmax(runMax_, celsius) - std::fmin(runMin_, celsius) > kSpread) {
                            settledSince_ = nowMs == 0 ? 1 : nowMs;
                            runMin_ = runMax_ = celsius;
                        }

                        runMin_ = std::fmin(runMin_, celsius);
                        runMax_ = std::fmax(runMax_, celsius);

                        if (nowMs - settledSince_ >= kSettledMs) {
                            begin(nowMs);
                            return true;
                        }

                        return false;

                    case kPulse:
                    case kPause:
                        if (!in.sensor || !in.tankOk || !in.ready || in.userActive || stopWanted_) {
                            stopWanted_ = false;
                            phase_ = kDone;
                            return false;
                        }

                        if (phase_ == kPulse) {
                            if (nowMs - phaseStart_ < kPulseMs) {
                                return true;
                            }

                            phaseStart_ = nowMs;
                            phase_ = pulse_ < kPulses ? kPause : kDone;
                            return false;
                        }

                        if (nowMs - phaseStart_ < kPauseMs) {
                            return false;
                        }

                        ++pulse_;
                        phaseStart_ = nowMs;
                        phase_ = kPulse;
                        return true;

                    default:
                        stopWanted_ = false;
                        return false;
                }
            }

            /** Start by hand (the next update() checks the conditions); also replaces a pending automatic one */
            void requestStart() {
                startWanted_ = true;
            }

            /** Stop a running flush by hand */
            void requestStop() {
                stopWanted_ = true;
            }

            Phase phase() const {
                return phase_;
            }

            bool running() const {
                return phase_ == kPulse || phase_ == kPause;
            }

            /** 1 to kPulses while running, 0 otherwise */
            int pulse() const {
                return running() ? pulse_ : 0;
            }

            /** since the first pulse began, for the display */
            uint32_t elapsedMs(const uint32_t nowMs) const {
                return running() ? nowMs - startMs_ : 0;
            }

        private:
            void begin(const uint32_t nowMs) {
                phase_ = kPulse;
                pulse_ = 1;
                phaseStart_ = nowMs;
                startMs_ = nowMs;
                settledSince_ = 0;
                stopWanted_ = false;
            }

            Phase phase_ = kNone;
            bool seen_ = false;
            bool startWanted_ = false;
            bool stopWanted_ = false;
            int pulse_ = 0;
            uint32_t settledSince_ = 0;
            double runMin_ = 0.0;
            double runMax_ = 0.0;
            uint32_t phaseStart_ = 0;
            uint32_t startMs_ = 0;
    };

} // namespace orione
