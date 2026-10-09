/**
 * @file OrioneDescale.h
 *
 * @brief Orione build: descaling as a program (Dominik, 09.10.2026: "Kalt mit Pausen, dann Klarspülen"). Cold and in
 *        short runs with soaking in between, as Quick Mill owners do it (Kaffee-Netz 85010, Sebastiano #455: the block
 *        and the lines empty after each pump stop, so the descaler only acts briefly each time):
 *        1. the heater is off; a warm block is waited for until kCoolCelsius,
 *        2. kRounds rounds of kPumpMs pumping and kSoakMs soaking,
 *        3. the rest of the solution through, in bursts, until the tank is empty,
 *        4. twice: the tank filled with clear water ("Weiter" on the page), through until it is empty.
 *        The water level sensor ends the runs; an empty tank during the rounds asks for more solution. Valve and pump
 *        always together (the valve alone makes the Pulsor board pulse the pump). No Arduino dependencies: tested in
 *        simulator/test/test_orione; on the machine in src/descaleProgram.h.
 */

#pragma once

#include <cstdint>

namespace orione {

    class DescaleProgram {
        public:
            enum Phase : uint8_t {
                kOff = 0,
                kCooling = 1,   // heater off, waiting for the block to cool down
                kDescale = 2,   // the rounds: pumping, then soaking
                kRefill = 3,    // the tank ran empty during the rounds: more solution, then "Weiter"
                kRest = 4,      // the rest of the solution through
                kWaitRinse = 5, // tank rinsed and filled with clear water, then "Weiter"
                kRinse = 6,     // clear water through (pass 1 or 2)
                kDone = 7,
            };

            static constexpr int kRounds = 8;
            static constexpr uint32_t kPumpMs = 10000;
            static constexpr uint32_t kSoakMs = 300000;
            static constexpr uint32_t kBurstMs = 20000;     // rest and rinse: pumping ...
            static constexpr uint32_t kBurstRestMs = 10000; // ... and a break (Ulka: 2 min on, 1 min off at most)
            static constexpr uint32_t kPassMaxMs = 900000;  // pumping per pass at most, in case the sensor never says empty
            static constexpr int kRinsePasses = 2;
            static constexpr float kCoolCelsius = 60.0f;

            struct Inputs {
                    bool tankFull;
                    float celsius;
            };

            void start(const uint32_t nowMs) {
                *this = DescaleProgram{};
                phase_ = kCooling;
                stepMs_ = nowMs;
            }

            void stop() {
                *this = DescaleProgram{};
            }

            /** "Weiter" on the page: after refilling (kRefill) or filling clear water (kWaitRinse); needs a full tank */
            bool next(const uint32_t nowMs, const bool tankFull) {
                if (!tankFull) {
                    return false;
                }

                if (phase_ == kRefill) {
                    phase_ = kDescale;
                    beginStep(nowMs, true);
                    return true;
                }

                if (phase_ == kWaitRinse) {
                    phase_ = kRinse;
                    pumpedMs_ = 0;
                    beginStep(nowMs, true);
                    return true;
                }

                return false;
            }

            /** @return true: valve and pump on now */
            bool update(const uint32_t nowMs, const Inputs& in) {
                const uint32_t inStep = nowMs - stepMs_;

                switch (phase_) {
                    case kCooling:
                        if (in.celsius <= kCoolCelsius) {
                            phase_ = kDescale;
                            round_ = 1;
                            beginStep(nowMs, true);
                        }
                        break;

                    case kDescale:
                        if (pumping_ && !in.tankFull) {
                            phase_ = kRefill; // not enough solution: this round's pumping again after refilling
                            pumping_ = false;
                        }
                        else if (pumping_ && inStep >= kPumpMs) {
                            beginStep(nowMs, false); // soak
                        }
                        else if (!pumping_ && inStep >= kSoakMs) {
                            if (round_ >= kRounds) {
                                phase_ = kRest;
                                pumpedMs_ = 0;
                                beginStep(nowMs, true);
                            }
                            else {
                                ++round_;
                                beginStep(nowMs, true);
                            }
                        }
                        break;

                    case kRest:
                    case kRinse:
                        if (pumping_) {
                            const bool over = pumpedMs_ + inStep >= kPassMaxMs;

                            if (!in.tankFull || over) {
                                pumpedMs_ += inStep;
                                endPass();
                            }
                            else if (inStep >= kBurstMs) {
                                pumpedMs_ += inStep;
                                beginStep(nowMs, false);
                            }
                        }
                        else if (!in.tankFull) {
                            endPass(); // ran empty during the break
                        }
                        else if (inStep >= kBurstRestMs) {
                            beginStep(nowMs, true);
                        }
                        break;

                    default:
                        pumping_ = false;
                        break;
                }

                return pumping_;
            }

            Phase phase() const {
                return phase_;
            }

            bool running() const {
                return phase_ != kOff && phase_ != kDone;
            }

            bool pumping() const {
                return pumping_;
            }

            /** 1..kRounds during the rounds, else 0 */
            int round() const {
                return phase_ == kDescale || phase_ == kRefill ? round_ : 0;
            }

            /** 1..kRinsePasses while rinsing or waiting for clear water, else 0 */
            int pass() const {
                return phase_ == kWaitRinse || phase_ == kRinse ? pass_ : 0;
            }

            /** Seconds left in the step of the rounds (pumping or soaking), -1 otherwise */
            int secondsLeft(const uint32_t nowMs) const {
                if (phase_ != kDescale) {
                    return -1;
                }

                const uint32_t length = pumping_ ? kPumpMs : kSoakMs, in = nowMs - stepMs_;
                return in >= length ? 0 : static_cast<int>((length - in + 999) / 1000);
            }

        private:
            void beginStep(const uint32_t nowMs, const bool pump) {
                stepMs_ = nowMs;
                pumping_ = pump;
            }

            void endPass() {
                pumping_ = false;

                if (phase_ == kRest) {
                    phase_ = kWaitRinse;
                    pass_ = 1;
                }
                else if (pass_ < kRinsePasses) {
                    phase_ = kWaitRinse;
                    ++pass_;
                }
                else {
                    phase_ = kDone;
                }
            }

            Phase phase_ = kOff;
            uint32_t stepMs_ = 0;
            uint32_t pumpedMs_ = 0;
            int round_ = 0;
            int pass_ = 0;
            bool pumping_ = false;
    };

} // namespace orione
