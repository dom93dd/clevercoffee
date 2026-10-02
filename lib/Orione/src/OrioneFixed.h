/**
 * @file OrioneFixed.h
 *
 * @brief Settings that are fixed on Dominik's Quick Mill Orione 3000 (Orione build, CC_ORIONE):
 *        not in the web interface and not changeable by a config upload. Config::get() answers
 *        these paths from this table before looking into config.json.
 *
 * The relay trigger type matters most: a missing or wrong value would mean LOW_TRIGGER, i.e.
 * heater, pump and valve switched on while "off" with the high-level SSR module (orione-full-build.md
 * chap. 6). The numbers are the firmware's enum values (Relay::TriggerType, Switch::Type,
 * Switch::Mode); Config.h checks them with static_assert. Unit-tested (simulator/test/test_orione).
 */

#pragma once

#include <cstring>

namespace orione {

    constexpr double kHighTrigger = 1;  // Relay::HIGH_TRIGGER
    constexpr double kToggle = 1;       // Switch::TOGGLE
    constexpr double kNormallyOpen = 0; // Switch::NORMALLY_OPEN
    constexpr double kBluetoothScale = 2;

    struct FixedSetting {
            const char* path;
            double value;
    };

    inline constexpr FixedSetting kFixedSettings[] = {
        {"hardware.relays.heater.trigger_type", kHighTrigger},
        {"hardware.relays.valve.trigger_type", kHighTrigger},
        {"hardware.relays.pump.trigger_type", kHighTrigger},
        {"hardware.switches.brew.enabled", 1},
        {"hardware.switches.brew.type", kToggle},
        {"hardware.switches.brew.mode", kNormallyOpen},
        {"hardware.sensors.scale.type", kBluetoothScale},
        {"system.offline_mode", 0},
    };

    /** The fixed value of a settings path, or nullptr if the path is an ordinary setting */
    inline const double* fixedValue(const char* path) {
        if (path == nullptr) {
            return nullptr;
        }

        for (const auto& f : kFixedSettings) {
            if (std::strcmp(f.path, path) == 0) {
                return &f.value;
            }
        }

        return nullptr;
    }

} // namespace orione
