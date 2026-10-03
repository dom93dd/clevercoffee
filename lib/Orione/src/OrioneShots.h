/**
 * @file OrioneShots.h
 *
 * @brief The last shots (time, weight, when) and their curves (weight and brew temperature over
 *        the shot) for the web interface's "Letzte Bezüge".
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
#include <limits>

namespace orione {

    enum Taste : uint8_t {
        kNotRated = 0,
        kSour = 1,
        kGood = 2,
        kBitter = 3,
    };

    struct Shot {
            float seconds = 0;
            float grams = -1;        // < 0: no scale connected
            uint32_t when = 0;       // UTC seconds since 1970, 0 = clock not set yet
            uint16_t seq = 0;        // running number; the curve is saved under seq % ShotLog::kSize
            uint16_t doseTenths = 0; // ground coffee in 0.1 g, 0 = not given
            char grind[10] = {};     // grinder setting as typed (e.g. "12" or "2.5")
            uint8_t taste = kNotRated;
            int16_t startTenths = 0;      // brew temperature at the start, 0.1 degrees, 0 = unknown
            uint16_t firstDropTenths = 0; // seconds until the first drops, 0.1 s, 0 = no scale
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

                shots_[0] = Shot{seconds, grams, when, static_cast<uint16_t>(count_ ? shots_[1].seq + 1 : 0)};
                count_ = count_ < kSize ? count_ + 1 : kSize;
                sinceBackflush_ = sinceBackflush_ < 0xFFFF ? sinceBackflush_ + 1 : sinceBackflush_;
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

            /** Dose and grinder setting of the newest shot (what was set up for it) */
            void noteRecipe(const float dose, const char* grind) {
                if (count_ == 0) {
                    return;
                }

                const float t = dose * 10.0f;
                shots_[0].doseTenths = std::isfinite(t) && t > 0.0f && t < 65535.0f ? static_cast<uint16_t>(t + 0.5f) : 0;
                std::memset(shots_[0].grind, 0, sizeof(shots_[0].grind));

                if (grind != nullptr) {
                    std::strncpy(shots_[0].grind, grind, sizeof(shots_[0].grind) - 1);
                }
            }

            /** Temperature at the start and the first drops of the newest shot (< 0: unknown) */
            void noteFacts(const float startCelsius, const float firstDropSeconds) {
                if (count_ == 0) {
                    return;
                }

                const float t = startCelsius * 10.0f, d = firstDropSeconds * 10.0f;
                shots_[0].startTenths = std::isfinite(t) && t > 0.0f && t < 3000.0f ? static_cast<int16_t>(t + 0.5f) : 0;
                shots_[0].firstDropTenths = std::isfinite(d) && d > 0.0f && d < 65535.0f ? static_cast<uint16_t>(d + 0.5f) : 0;
            }

            /** @return false if there is no such shot or no such taste */
            bool rate(const int i, const uint8_t taste) {
                if (i < 0 || i >= count_ || taste > kBitter) {
                    return false;
                }

                shots_[i].taste = taste;
                return true;
            }

            uint16_t sinceBackflush() const {
                return sinceBackflush_;
            }

            void backflushDone() {
                sinceBackflush_ = 0;
            }

            /** @return true if a shot was waiting for its drops: save it now (the next shot starts) */
            bool settleNow() {
                const bool was = settling_;
                settling_ = false;
                return was;
            }

            bool settling() const {
                return settling_;
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
                    uint16_t sinceBackflush;
                    Shot shots[kSize];
            };

            Stored stored() const {
                Stored s{};
                s.version = kVersion;
                s.count = static_cast<uint8_t>(count_);
                s.sinceBackflush = sinceBackflush_;
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
                sinceBackflush_ = s.sinceBackflush;
                std::memcpy(shots_, s.shots, sizeof(shots_));

                for (auto& shot : shots_) {
                    shot.grind[sizeof(shot.grind) - 1] = '\0';
                }

                return true;
            }

        private:
            static constexpr uint8_t kVersion = 4; // 2: running number for the curves, 3: dose, grind, taste, backflush counter, 4: start temperature, first drops

            Shot shots_[kSize];
            int count_ = 0;
            uint16_t sinceBackflush_ = 0;
            uint32_t settleStart_ = 0;
            bool settling_ = false;
    };

    /**
     * Weight and brew temperature during a shot, one point every interval from the start of the
     * brew until the log is saved (so the drops after the stop are on it too). When the buffer is
     * full, neighbouring points are merged into one and the interval doubles: a long shot still
     * fits, at a coarser step. Tenths as int16 (0.1 g, 0.1 °C); kNone without a value (no scale).
     */
    class ShotCurve {
        public:
            static constexpr int kMaxPoints = 100;
            static constexpr uint16_t kStartIntervalMs = 500;
            static constexpr int16_t kNone = std::numeric_limits<int16_t>::min();

            struct Point {
                    int16_t grams;
                    int16_t celsius;
                    int16_t flow; // 0.01 g/s
            };

            // Saved form (one per shot): version, interval, stop point, points
            struct Stored {
                    uint8_t version;
                    uint8_t count;
                    int16_t stop; // first point after the pump stopped, -1 = still running
                    uint16_t intervalMs;
                    Point points[kMaxPoints];
            };

            void begin(const uint32_t nowMs) {
                count_ = 0;
                stop_ = -1;
                intervalMs_ = kStartIntervalMs;
                startMs_ = nowMs;
                recording_ = true;
            }

            /** Call often; takes a point when the next one is due (point k at k * interval). Flow in g/s, < 0 without scale */
            void sample(const uint32_t nowMs, const float grams, const float celsius, const float flow = -1.0f) {
                if (!recording_ || nowMs - startMs_ < static_cast<uint32_t>(count_) * intervalMs_) {
                    return;
                }

                if (count_ == kMaxPoints) {
                    halve();
                }

                const float f = std::round(flow * 100.0f);
                points_[count_++] = Point{grams < 0 ? kNone : tenths(grams), tenths(celsius), flow < 0 || !std::isfinite(flow) ? kNone : static_cast<int16_t>(f > 32767.0f ? 32767.0f : f)};
            }

            /** The pump stopped: what follows are the drops */
            void stopped() {
                if (recording_ && stop_ < 0) {
                    stop_ = static_cast<int16_t>(count_);
                }
            }

            void end() {
                recording_ = false;
            }

            bool recording() const {
                return recording_;
            }

            int count() const {
                return count_;
            }

            int stop() const {
                return stop_;
            }

            uint16_t intervalMs() const {
                return intervalMs_;
            }

            const Point& at(const int i) const {
                return points_[i];
            }

            Stored stored() const {
                Stored s{};
                s.version = kVersion;
                s.count = static_cast<uint8_t>(count_);
                s.stop = stop_;
                s.intervalMs = intervalMs_;
                std::memcpy(s.points, points_, sizeof(points_));
                return s;
            }

            bool restore(const void* data, const size_t length) {
                Stored s{};

                if (data == nullptr || length != sizeof(s)) {
                    return false;
                }

                std::memcpy(&s, data, sizeof(s));

                if (s.version != kVersion || s.count > kMaxPoints || s.stop > s.count || s.intervalMs == 0) {
                    return false;
                }

                count_ = s.count;
                stop_ = s.stop;
                intervalMs_ = s.intervalMs;
                recording_ = false;
                std::memcpy(points_, s.points, sizeof(points_));
                return true;
            }

        private:
            static constexpr uint8_t kVersion = 2; // 2: flow

            static int16_t tenths(const float v) {
                if (!std::isfinite(v)) {
                    return kNone;
                }

                const float t = std::round(v * 10.0f);
                return static_cast<int16_t>(t > 32767.0f ? 32767.0f : t < -32767.0f ? -32767.0f : t);
            }

            /** Every second point stays: same duration at half the points */
            void halve() {
                for (int i = 0; i < kMaxPoints / 2; ++i) {
                    points_[i] = points_[2 * i];
                }

                count_ = kMaxPoints / 2;
                intervalMs_ = static_cast<uint16_t>(intervalMs_ * 2);

                if (stop_ >= 0) {
                    stop_ = static_cast<int16_t>((stop_ + 1) / 2);
                }
            }

            Point points_[kMaxPoints] = {};
            int count_ = 0;
            int16_t stop_ = -1;
            uint16_t intervalMs_ = kStartIntervalMs;
            uint32_t startMs_ = 0;
            bool recording_ = false;
    };

} // namespace orione
