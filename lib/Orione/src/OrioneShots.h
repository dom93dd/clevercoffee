/**
 * @file OrioneShots.h
 *
 * @brief The last shots (time, weight, when) and their curves (weight and brew temperature over
 *        the shot) for the web interface's "Letzte Bezüge".
 *
 * A shot is recorded when the brew ends. The scale keeps counting the drops for a few seconds
 * after the pump stopped, so the weight is updated until then (the highest reading: lifting the
 * cup must not count as zero) and only then is the log due for saving. What is not a shot is left
 * out: with a scale connected, under kMinGrams in the cup (a flush through the group however long,
 * Dominik 08.10.2026; also a shot with the cup not on the scale); without one, under kMinSeconds
 * (a slip of the switch). A shot that ran through fast with coffee in the cup counts: just what one
 * wants to see. No Arduino dependencies: tested in simulator/test/test_orione.
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
            uint16_t targetTenths = 0;    // target weight when stopped by weight, 0.1 g, 0 = not by weight
            uint16_t stopTenths = 0;      // in the cup when the pump stopped, 0.1 g (grams: after the drops)
            uint8_t leadTenths = 0;       // the pump stopped this far before the target, 0.1 g
            char beans[41] = {};          // beans in the grinder then (setting brew.beans, up to 40 bytes), as typed
    };

    /**
     * Brew by weight: the pump stops a lead before the target, because the scale reports late and drops
     * follow. The lead learns from each shot stopped by weight, after tatemazer's shotStopper
     * (AcaiaArduinoBLE, examples/shotStopper: offset += final weight - goal, unchanged if the error is
     * over 5 g), but gentler: half the error per shot and nothing from errors over 3 g. Taking the whole
     * error, one shot 3.5 g over (poured on by hand at the bench) moved the lead from 1.5 to its 5 g
     * limit (Dominik, 06.10.2026: "ja bitte entschärfen"); half of it settles a steady drip in a few shots.
     */
    struct BrewLead {
            static constexpr float kStart = 1.5f; // shotStopper's default
            static constexpr float kMax = 5.0f;
            static constexpr float kMaxError = 3.0f;
            static constexpr float kGain = 0.5f;

            /** @return the lead for the next shot after one that ended with finalGrams in the cup */
            static float learn(const float lead, const float target, const float finalGrams) {
                const float error = finalGrams - target;

                if (!std::isfinite(lead) || !std::isfinite(error) || target <= 0.0f || std::fabs(error) > kMaxError) {
                    return lead;
                }

                const float next = lead + kGain * error;
                return next < 0.0f ? 0.0f : next > kMax ? kMax : next;
            }
    };

    class ShotLog {
        public:
            static constexpr int kSize = 5;
            static constexpr float kMinSeconds = 10.0f; // without a scale
            static constexpr float kMinGrams = 5.0f;    // with a scale
            static constexpr uint32_t kSettleMs = 4000;

            /** @return false if it was not a shot (see above) */
            bool record(const float seconds, const float grams, const uint32_t when, const uint32_t nowMs) {
                if (!std::isfinite(seconds) || seconds <= 0.0f || !(grams >= 0.0f ? grams >= kMinGrams : seconds >= kMinSeconds)) {
                    return false;
                }

                for (int i = kSize - 1; i > 0; --i) {
                    shots_[i] = shots_[i - 1];
                }

                count_ = count_ < kSize ? count_ + 1 : kSize;
                shots_[0] = Shot{seconds, grams, when, nextSeq()};
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

            /** Dose, grinder setting and beans of the newest shot (what was set up for it) */
            void noteRecipe(const float dose, const char* grind, const char* beans = nullptr) {
                if (count_ == 0) {
                    return;
                }

                const float t = dose * 10.0f;
                shots_[0].doseTenths = std::isfinite(t) && t > 0.0f && t < 65535.0f ? static_cast<uint16_t>(t + 0.5f) : 0;
                copyText(shots_[0].grind, sizeof(shots_[0].grind), grind);
                copyText(shots_[0].beans, sizeof(shots_[0].beans), beans);
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

            /** Brew by weight of the newest shot: its target (<= 0: not stopped by weight), what was in the
             *  cup when the pump stopped, and the lead it stopped with */
            void noteWeights(const float target, const float atStop, const float lead) {
                if (count_ == 0) {
                    return;
                }

                const auto tenths = [](const float g, const float max) {
                    const float t = g * 10.0f;
                    return std::isfinite(t) && t > 0.0f && t < max ? static_cast<int>(t + 0.5f) : 0;
                };
                shots_[0].targetTenths = static_cast<uint16_t>(tenths(target, 65535.0f));
                shots_[0].stopTenths = static_cast<uint16_t>(tenths(atStop, 65535.0f));
                shots_[0].leadTenths = static_cast<uint8_t>(tenths(lead, 255.0f));
            }

            /**
             * @brief Delete shot i (0 = newest), e.g. a test at the bench. The others keep their running
             *        numbers and with them their curves. Counted since the last backflush: one less.
             * @return false if there is no such shot
             */
            bool remove(const int i) {
                if (i < 0 || i >= count_) {
                    return false;
                }

                if (i == 0) {
                    settling_ = false; // its drops are no longer wanted (and the next newest has its own)
                }

                if (i < sinceBackflush_) {
                    --sinceBackflush_; // the newest sinceBackflush_ shots came after the last backflush
                }

                for (int k = i; k < kSize - 1; ++k) {
                    shots_[k] = shots_[k + 1];
                }

                shots_[kSize - 1] = Shot{};
                --count_;
                return true;
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
                    shot.beans[sizeof(shot.beans) - 1] = '\0';
                }

                return true;
            }

        private:
            /** Typed text into a fixed field; cut where it does not fit, but never inside a UTF-8 letter (ü, é) */
            static void copyText(char* out, const size_t size, const char* in) {
                std::memset(out, 0, size);

                if (in == nullptr) {
                    return;
                }

                size_t n = std::strlen(in);

                if (n > size - 1) {
                    n = size - 1;

                    while (n > 0 && (static_cast<unsigned char>(in[n]) & 0xC0) == 0x80) {
                        --n; // in[n] continues a letter that began before: cut before that letter
                    }
                }

                std::memcpy(out, in, n);
            }

            /**
             * Running number of the new shots_[0]: one after the previous newest, skipping numbers whose
             * curve slot (seq % kSize) a kept shot still uses. After a delete the numbers have a gap, and
             * the plain next number could land on the slot of a shot that is still listed.
             */
            uint16_t nextSeq() const {
                if (count_ < 2) {
                    return 0; // the only one
                }

                uint16_t seq = static_cast<uint16_t>(shots_[1].seq + 1);

                for (int tries = 0; tries < kSize; ++tries, ++seq) {
                    bool used = false;

                    for (int k = 1; k < count_; ++k) {
                        used = used || shots_[k].seq % kSize == seq % kSize;
                    }

                    if (!used) {
                        break;
                    }
                }

                return seq;
            }

            static constexpr uint8_t kVersion = 6; // 2: running number for the curves, 3: dose, grind, taste, backflush counter, 4: start temperature, first drops, 5: target, weight at the stop, lead, 6: beans

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
