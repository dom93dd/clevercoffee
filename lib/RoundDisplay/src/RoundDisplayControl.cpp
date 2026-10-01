/**
 * @file RoundDisplayControl.cpp
 */

#include "RoundDisplayControl.h"

#include <cctype>
#include <cstring>

namespace rd {

    Mode modeFromMachineState(const int machineState) {
        switch (machineState) {
            case firmware::kPidNormal:
                return Mode::Normal;
            case firmware::kBrew:
                return Mode::Brew;
            case firmware::kManualFlush:
                return Mode::ManualFlush;
            case firmware::kSteam:
                return Mode::Steam;
            case firmware::kHotWater:
                return Mode::HotWater;
            case firmware::kBackflush:
                return Mode::Backflush;
            case firmware::kPidDisabled:
                return Mode::PidDisabled;
            case firmware::kWaterTankEmpty:
                return Mode::WaterTankEmpty;
            case firmware::kStandby:
                return Mode::Standby;
            case firmware::kEmergencyStop:
                return Mode::EmergencyStop;
            case firmware::kSensorError:
                return Mode::SensorError;
            default:
                return Mode::Init;
        }
    }

    BrewPhase brewPhaseFromState(const int brewState) {
        switch (brewState) {
            case firmware::kPreinfusion:
                return BrewPhase::Preinfusion;
            case firmware::kPreinfusionPause:
                return BrewPhase::PreinfusionPause;
            case firmware::kBrewRunning:
                return BrewPhase::Running;
            case firmware::kBrewFinished:
                return BrewPhase::Finished;
            default:
                return BrewPhase::Idle;
        }
    }

    BackflushPhase backflushPhaseFromState(const int backflushState) {
        switch (backflushState) {
            case firmware::kBackflushFilling:
                return BackflushPhase::Filling;
            case firmware::kBackflushFlushing:
                return BackflushPhase::Flushing;
            case firmware::kBackflushEnding:
                return BackflushPhase::Ending;
            case firmware::kBackflushFinished:
                return BackflushPhase::Finished;
            default:
                return BackflushPhase::Idle;
        }
    }

    bool BrewTimer::update(const bool brewActive, const float brewTimeSeconds, const uint32_t nowMs, const float holdSeconds) {
        switch (state_) {
            case State::Idle:
                if (brewActive) {
                    state_ = State::Running;
                }
                break;

            case State::Running:
                if (!brewActive) {
                    state_ = State::Hold;
                    endMs_ = nowMs;
                    lastShot_ = brewTimeSeconds; // the firmware keeps the final time until the next shot
                }
                break;

            case State::Hold:
                if (brewActive) {
                    state_ = State::Running; // next shot during the hold time
                }
                else if (nowMs - endMs_ > static_cast<uint32_t>(holdSeconds * 1000.0f)) {
                    state_ = State::Idle;
                }
                break;
        }

        return state_ != State::Idle;
    }

    Message MessageText::message() const {
        Message m;
        const char* slots[kLines] = {};

        for (int i = 0; i < count && i < kLines; ++i) {
            slots[i] = lines[i];
        }

        m.title = slots[0];
        m.line1 = slots[1];
        m.line2 = slots[2];
        m.line3 = slots[3];
        return m;
    }

    namespace {
        /** Copies at most size - 1 bytes without cutting a UTF-8 character in half */
        void copyUtf8(char* dst, const size_t size, const char* src, size_t len) {
            if (len > size - 1) {
                len = size - 1;

                // Step back over continuation bytes (10xxxxxx) and the start byte they belong to
                while (len > 0 && (static_cast<uint8_t>(src[len]) & 0xC0) == 0x80) {
                    --len;
                }
            }

            memcpy(dst, src, len);
            dst[len] = '\0';
        }

        void trim(char* s) {
            size_t len = strlen(s);

            while (len > 0 && isspace(static_cast<unsigned char>(s[len - 1]))) {
                s[--len] = '\0';
            }

            size_t start = 0;

            while (s[start] != '\0' && isspace(static_cast<unsigned char>(s[start]))) {
                ++start;
            }

            if (start > 0) {
                memmove(s, s + start, len - start + 1);
            }
        }

        /** ASCII and the Latin-1 letters à..þ (UTF-8 C3 A0..C3 BE, not ÷) to capitals */
        void toCapitals(char* s) {
            for (auto* p = reinterpret_cast<unsigned char*>(s); *p != '\0'; ++p) {
                if (*p >= 'a' && *p <= 'z') {
                    *p = static_cast<unsigned char>(*p - 'a' + 'A');
                }
                else if (*p == 0xC3 && p[1] >= 0xA0 && p[1] <= 0xBE && p[1] != 0xB7) {
                    p[1] = static_cast<unsigned char>(p[1] - 0x20);
                    ++p;
                }
            }
        }
    } // namespace

    void splitMessage(const char* text, MessageText& out) {
        out = MessageText();

        if (text == nullptr) {
            return;
        }

        const char* start = text;

        while (out.count < MessageText::kLines) {
            const char* end = strchr(start, '\n');
            const size_t len = end != nullptr ? static_cast<size_t>(end - start) : strlen(start);
            char* line = out.lines[out.count];
            copyUtf8(line, MessageText::kLength, start, len);
            trim(line);

            // A short first line is a title and shown in capitals; long sentences keep their case
            if (out.count == 0 && strlen(line) <= MessageText::kTitleLength) {
                toCapitals(line);
            }

            ++out.count;

            if (end == nullptr) {
                break;
            }

            start = end + 1;
        }
    }

    PowerSequencer::Action PowerSequencer::update(const bool sleepWanted, RoundUi& ui, const uint32_t nowMs) {
        if (sleepWanted && !asleep_) {
            if (!closing_) {
                ui.play(Animation::Close, nowMs);
                closing_ = true;
                return Action::None;
            }

            if (!ui.animating(nowMs)) {
                asleep_ = true;
                closing_ = false;
                return Action::Sleep;
            }

            return Action::None;
        }

        if (!sleepWanted && asleep_) {
            asleep_ = false;
            ui.invalidate();
            ui.play(Animation::Reveal, nowMs);
            return Action::Wake;
        }

        if (!sleepWanted && closing_) {
            // Woken up again before the iris was closed: open it again
            closing_ = false;
            ui.play(Animation::Reveal, nowMs);
        }

        return Action::None;
    }

    uint32_t pixelHash(const uint16_t* pixels, const size_t count) {
        uint32_t h = 2166136261u;

        for (size_t i = 0; i < count; ++i) {
            h = (h ^ pixels[i]) * 16777619u;
        }

        return h;
    }

    bool BandFilter::changed(const int index, const uint16_t* pixels, const size_t count) {
        if (index < 0 || index >= kMaxBands) {
            return true;
        }

        const uint32_t h = pixelHash(pixels, count);
        const uint32_t bit = 1u << index;

        if ((shown_ & bit) != 0 && hash_[index] == h) {
            return false;
        }

        hash_[index] = h;
        shown_ |= bit;
        return true;
    }

} // namespace rd
