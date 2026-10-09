/**
 * @file RoundDisplayControl.h
 *
 * @brief Logic between the firmware and the UI that does not need the hardware, so it can be
 *        tested on the desktop: state mapping, brew timer, message splitting, display power.
 */

#pragma once

#include "RoundDisplayModel.h"
#include "RoundDisplayUi.h"

#include <cstddef>
#include <cstdint>

namespace rd {

    /**
     * Numbers of the firmware enums (MachineState in main.cpp, BrewState and BackflushState in
     * brewStates.h). src/display/roundDisplay.h checks them with static_assert.
     */
    namespace firmware {
        constexpr int kInit = 0;
        constexpr int kPidNormal = 10;
        constexpr int kBrew = 20;
        constexpr int kManualFlush = 25;
        constexpr int kSteam = 30;
        constexpr int kHotWater = 40;
        constexpr int kBackflush = 50;
        constexpr int kPidDisabled = 60;
        constexpr int kWaterTankEmpty = 70;
        constexpr int kStandby = 80;
        constexpr int kEmergencyStop = 100;
        constexpr int kSensorError = 110;

        constexpr int kBrewIdle = 10;
        constexpr int kPreinfusion = 20;
        constexpr int kPreinfusionPause = 30;
        constexpr int kBrewRunning = 40;
        constexpr int kBrewFinished = 50;

        constexpr int kBackflushIdle = 10;
        constexpr int kBackflushFilling = 20;
        constexpr int kBackflushFlushing = 30;
        constexpr int kBackflushEnding = 40;
        constexpr int kBackflushFinished = 50;
    } // namespace firmware

    Mode modeFromMachineState(int machineState);
    BrewPhase brewPhaseFromState(int brewState);
    BackflushPhase backflushPhaseFromState(int backflushState);

    /**
     * @brief Whether the brew timer is visible: while brewing and for a hold time afterwards
     *        (same behaviour as shouldDisplayBrewTimer() in displayCommon.h). Also keeps the
     *        duration of the last shot.
     */
    class BrewTimer {
        public:
            static constexpr uint32_t kRemindMs = 60000; // brew switch still on this long after the shot: remind

            /**
             * @param switchHeld the brew switch is still on after a shot that stopped by itself (time or
             *        weight): the result stays until it is switched off, then holdSeconds more (Dominik,
             *        07.10.2026). A shot stopped by switching off holds holdSeconds as before.
             */
            bool update(bool brewActive, float brewTimeSeconds, uint32_t nowMs, float holdSeconds, bool switchHeld = false);

            float lastShotSeconds() const {
                return lastShot_;
            }

            /** Not a shot after all (the rinse after one): no result held, the last shot's time stays */
            void cancel() {
                state_ = State::Idle;
                held_ = false;
            }

            /** The switch has been left on for kRemindMs after the shot: show a hint to switch it off */
            /** The result stays because the brew switch is still on */
            bool held() const {
                return state_ == State::Hold && held_;
            }

            bool remind(const uint32_t nowMs) const {
                return state_ == State::Hold && held_ && nowMs - heldSince_ >= kRemindMs;
            }

        private:
            enum class State : uint8_t {
                Idle,
                Running,
                Hold,
            };

            State state_ = State::Idle;
            uint32_t endMs_ = 0;
            float lastShot_ = 0;
            bool held_ = false;
            uint32_t heldSince_ = 0;
    };

    /**
     * @brief Text of a message screen, split from the firmware's "Title\nline\nline" strings.
     *        A short title is converted to capitals (ASCII and Latin-1 letters such as ä -> Ä).
     */
    struct MessageText {
            static constexpr int kLines = 4;
            static constexpr int kLength = 128;        // the calibration messages are one long sentence
            static constexpr size_t kTitleLength = 24; // longer first lines are sentences, not titles
            char lines[kLines][kLength] = {};
            int count = 0;

            Message message() const;
    };

    void splitMessage(const char* text, MessageText& out);

    /**
     * @brief Display off and on (u8g2->setPowerSave() in the firmware): closes the iris before the
     *        panel sleeps and opens it again after waking up. Call once per loop.
     */
    class PowerSequencer {
        public:
            enum class Action : uint8_t {
                None,
                Sleep, // put the panel to sleep now
                Wake,  // wake the panel up now
            };

            Action update(bool sleepWanted, RoundUi& ui, uint32_t nowMs);

            bool asleep() const {
                return asleep_;
            }

            bool closing() const {
                return closing_;
            }

        private:
            bool asleep_ = false;
            bool closing_ = false;
    };

    /** FNV-1a hash of pixel data, to notice whether a band changed */
    uint32_t pixelHash(const uint16_t* pixels, size_t count);

    /**
     * @brief Sends only the bands that differ from what the panel already shows. A 240x40 band takes
     *        5.7 ms over SPI at 27 MHz; when the temperature changes by 0.1 degrees, only the bands
     *        with the digits and the marker change, the rest of the frame need not be sent again.
     */
    class BandFilter {
        public:
            static constexpr int kMaxBands = 16;

            /** True if the band must be sent (then it counts as shown) */
            bool changed(int index, const uint16_t* pixels, size_t count);

            /** The panel content is unknown (start, after sleep): send every band next time */
            void invalidate() {
                shown_ = 0;
            }

        private:
            uint32_t hash_[kMaxBands] = {};
            uint32_t shown_ = 0; // bit per band
    };

} // namespace rd
