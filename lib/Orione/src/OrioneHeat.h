/**
 * @file OrioneHeat.h
 *
 * @brief The Orione's thermoblock beyond the PID controller: heating along during a shot, and steam
 *        from the original steam switch, which the firmware does not see. No Arduino dependencies:
 *        tested in simulator/test/test_orione.
 */

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace orione {

    /**
     * During a shot cold water flows through the thermoblock: at 2 g/s from 22 to 93 °C that alone takes
     * 2 g/s x 4.19 J/(g K) x 71 K ~ 600 W, some 60 % of the Orione's block (1000-1080 W). The controller
     * only reacts once the sensor on the outside of the block reports the drop, a few seconds late, and
     * the temperature falls (Dominik, 08.10.2026: "Könnern wir währendem Bezug eigentlich noch mitheizen?").
     * So while the pump runs the heater gets at least a set share of its power from the first second on;
     * the controller may give more. Over the setpoint (a slow, choked shot) the floor ends and the
     * controller alone decides, so a shot does not overheat.
     */
    struct BrewHeatBoost {
            static constexpr double kGuard = 0.5; // K over the setpoint: the floor ends

            /**
             * @param output the controller's output, 0..window
             * @param percent the floor in percent of the window (0 = off)
             * @return the heater output to use
             */
            static double apply(const double output, const double window, const double percent, const bool pumping, const double celsius, const double setpoint) {
                if (!pumping || !(percent > 0.0) || !std::isfinite(celsius) || !std::isfinite(setpoint) || celsius >= setpoint + kGuard) {
                    return output;
                }

                return std::max(output, window * std::min(percent, 100.0) / 100.0);
            }
    };

    /**
     * Steam (variant B, orione-full-build.md chap. 7): the steam switch S2 stays on mains and heats the
     * block through the steam thermostat (~120-130 °C) past the heater SSR; the firmware only sees the
     * temperature. The controller never takes the block that far (setpoint ~93 °C, overshoot a few K),
     * so from kSteamAbove on it is steam. Once it falls below kSteamBelow again S2 is off and the block
     * cools down; back to normal kNearAbove over the setpoint.
     */
    class SteamWatch {
        public:
            enum Phase : uint8_t {
                kNone = 0,
                kSteam = 1,   // steam thermostat heating
                kCooling = 2, // after steam: too hot for espresso until it has cooled down
            };

            static constexpr double kSteamAbove = 105.0;
            static constexpr double kSteamBelow = 103.0; // a little hysteresis: no flicker at the edge
            static constexpr double kNearAbove = 3.0;

            Phase update(const double celsius, const double setpoint) {
                if (!std::isfinite(celsius) || celsius <= 0.0) {
                    return phase_; // no reading (or the sensor's error value): keep what it was
                }

                if (celsius >= kSteamAbove) {
                    phase_ = kSteam;
                }
                else if (phase_ == kSteam && celsius < kSteamBelow) {
                    phase_ = kCooling;
                }
                else if (phase_ == kCooling && celsius <= setpoint + kNearAbove) {
                    phase_ = kNone;
                }

                return phase_;
            }

            Phase phase() const {
                return phase_;
            }

        private:
            Phase phase_ = kNone;
    };

} // namespace orione
