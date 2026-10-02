/**
 * @file TempSensorFake.h
 *
 * @brief Bench build only (-D CC_FAKE_TEMP_SENSOR, env esp32_round_bench): a simulated thermoblock
 *        instead of the TSIC, so heating, ready, shot and the web interface can be tried on the desk
 *        without the machine. NEVER in the machine: the heater would be switched on a made-up
 *        temperature. The build refuses CC_FAKE_TEMP_SENSOR outside the measurement build.
 *
 * Model (own rough numbers, not measured on the Orione): the heater output (pidOutput, 0..1000 per
 * window) reaches the block with an 8 s lag and heats it by up to 1 K/s; the block loses heat to a
 * 22 degree room with a 300 s time constant; water through the pump (shot) cools it by 1.5 K/s.
 */

#pragma once

#if defined(CC_FAKE_TEMP_SENSOR) && !defined(ROUND_TIMING)
#error "CC_FAKE_TEMP_SENSOR is for the bench build esp32_round_bench only, never for the machine"
#endif

#include "Logger.h"
#include "TempSensor.h"
#include "hardware/Relay.h"

#include <Arduino.h>

extern double pidOutput;
extern unsigned int windowSize;
extern GPIOPin* pumpRelayPin;

class TempSensorFake final : public TempSensor {
    public:
        TempSensorFake() {
            LOG(WARNING, "FAKE TEMPERATURE SENSOR: simulated thermoblock, bench build only - never use in the machine");
        }

    protected:
        bool sample_temperature(double& temperature) const override {
            const uint32_t now = millis();
            const double dt = lastMs_ == 0 ? 0.0 : (now - lastMs_) / 1000.0;
            lastMs_ = now;

            const double duty = constrain(pidOutput / static_cast<double>(windowSize), 0.0, 1.0);
            heat_ += (duty - heat_) * std::min(dt / kHeaterLagS, 1.0);
            const bool pumping = pumpRelayPin != nullptr && pumpRelayPin->read() == HIGH; // high-level trigger module

            block_ += dt * (heat_ * kHeatPerS - (block_ - kRoomC) / kLossTauS - (pumping ? kFlowCoolPerS : 0.0));
            temperature = block_ + (static_cast<int>(now / 400) % 3 - 1) * 0.02; // a little sensor noise

            if (now - lastWarnMs_ > 60000) {
                lastWarnMs_ = now;
                LOGF(WARNING, "FAKE TEMPERATURE SENSOR active (%.1f C) - bench build only", block_);
            }

            return true;
        }

    private:
        static constexpr double kRoomC = 22.0;
        static constexpr double kHeatPerS = 1.0;
        static constexpr double kHeaterLagS = 8.0;
        static constexpr double kLossTauS = 300.0;
        static constexpr double kFlowCoolPerS = 1.5;

        mutable double block_ = kRoomC;
        mutable double heat_ = 0.0;
        mutable uint32_t lastMs_ = 0;
        mutable uint32_t lastWarnMs_ = 0;
};
