/**
 * @file OrioneShots.h
 *
 * @brief The last shots (time, weight, when) for the web interface's "Letzte Bezüge".
 *
 * A shot is recorded when the brew ends. The scale keeps counting the drops for a few seconds
 * after the pump stopped, so the weight is updated until then (the highest reading: lifting the
 * cup must not count as zero) and only then is the log due for saving. Short runs are left out
 * (a flush through the group, a slip of the switch) unless the scale saw coffee in the cup: a shot
 * that ran through fast is just what one wants to see. No Arduino dependencies: tested in
 * simulator/test/test_orione.
 */

#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace orione {

    struct Shot {
            float seconds = 0;
            float grams = -1;  // < 0: no scale connected
            uint32_t when = 0; // UTC seconds since 1970, 0 = clock not set yet
    };

    class ShotLog {
        public:
            static constexpr int kSize = 5;
            static constexpr float kMinSeconds = 10.0f; // shorter only with coffee on the scale
            static constexpr float kMinGrams = 5.0f;
            static constexpr uint32_t kSettleMs = 4000;

            /** @return false if it was not a shot (too short and no coffee on the scale) */
            bool record(const float seconds, const float grams, const uint32_t when, const uint32_t nowMs) {
                if (!std::isfinite(seconds) || seconds <= 0.0f || !(seconds >= kMinSeconds || grams >= kMinGrams)) {
                    return false;
                }

                for (int i = kSize - 1; i > 0; --i) {
                    shots_[i] = shots_[i - 1];
                }

                shots_[0] = Shot{seconds, grams, when};
                count_ = count_ < kSize ? count_ + 1 : kSize;
                settleStart_ = nowMs;
                settling_ = true;
                return true;
            }

            /**
             * @brief Call regularly after a shot.
             * @param grams current brew weight, < 0 if the scale has none
             * @return true once, when the drops are counted and the log should be saved
             */
            bool settle(const uint32_t nowMs, const float grams) {
                if (!settling_) {
                    return false;
                }

                if (nowMs - settleStart_ < kSettleMs) {
                    if (grams > shots_[0].grams) {
                        shots_[0].grams = grams;
                    }

                    return false;
                }

                settling_ = false;
                return true;
            }

            int count() const {
                return count_;
            }

            /** 0 = newest */
            const Shot& at(const int i) const {
                return shots_[i];
            }

            // Saved form: a version byte, the count and the shots
            struct Stored {
                    uint8_t version;
                    uint8_t count;
                    Shot shots[kSize];
            };

            Stored stored() const {
                Stored s{};
                s.version = kVersion;
                s.count = static_cast<uint8_t>(count_);
                std::memcpy(s.shots, shots_, sizeof(shots_));
                return s;
            }

            /** @return false (and the log stays empty) if the data is not a saved log of this version */
            bool restore(const void* data, const size_t length) {
                Stored s{};

                if (data == nullptr || length != sizeof(s)) {
                    return false;
                }

                std::memcpy(&s, data, sizeof(s));

                if (s.version != kVersion || s.count > kSize) {
                    return false;
                }

                count_ = s.count;
                std::memcpy(shots_, s.shots, sizeof(shots_));
                return true;
            }

        private:
            static constexpr uint8_t kVersion = 1;

            Shot shots_[kSize];
            int count_ = 0;
            uint32_t settleStart_ = 0;
            bool settling_ = false;
    };

} // namespace orione
