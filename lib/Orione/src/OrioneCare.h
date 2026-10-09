/**
 * @file OrioneCare.h
 *
 * @brief What professional machines do besides the shot (Dominik, 08.10.2026: "die schnellen funktionen und temp
 *        verlauf + kanalbildung finde ich mega"): a heating schedule, waking from standby by the brew switch, counters
 *        with an estimate of the water through the thermoblock for descaling, a cleaning program with detergent, a
 *        temperature course during the shot and a check of the flow for channeling. No Arduino dependencies: tested in simulator/test/test_orione.
 */

#pragma once

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace orione {

    /**
     * The heating schedule as the page sets it (Dominik, 09.10.2026: "Je Wochentag eigene Zeiten"): per weekday up to
     * kWindows windows, each an on time and an optional off time, as the text of the setting schedule.plan:
     * Monday to Sunday separated by ';', windows by ',', a window "HH:MM-HH:MM" or "HH:MM" (no off time), an empty day
     * has none. Example: "06:30-09:00,17:00-20:00;06:30-09:00;;;;08:00-11:00;08:00-11:00". An off time before the on
     * time is on the next day (over midnight).
     */
    struct WeekPlan {
            static constexpr int kWindows = 2;
            static constexpr uint16_t kNone = 0xFFFF;
            static constexpr size_t kMaxText = 7 * (kWindows * 12) + 8; // "HH:MM-HH:MM," per window, ';' per day

            struct Window {
                    uint16_t on = kNone;  // minute of the day, kNone: no window
                    uint16_t off = kNone; // kNone: no off time
            };

            Window days[7][kWindows];

            /** @return false if the text is not a plan (out is left empty then) */
            static bool parse(const char* text, WeekPlan& out) {
                out = WeekPlan{};

                if (text == nullptr) {
                    return false;
                }

                int day = 0, window = 0;
                const char* p = text;

                while (true) {
                    if (*p == ';' || *p == '\0') {
                        if (*p == '\0') {
                            if (day != 6) {
                                out = WeekPlan{};
                            }

                            return day == 6;
                        }

                        if (++day > 6) {
                            out = WeekPlan{};
                            return false;
                        }

                        window = 0;
                        ++p;
                        continue;
                    }

                    if (*p == ',') {
                        ++p;
                        continue;
                    }

                    int on = -1, off = -1;

                    if (!time(p, on) || window >= kWindows) {
                        out = WeekPlan{};
                        return false;
                    }

                    if (*p == '-') {
                        ++p;

                        if (*p != ',' && *p != ';' && *p != '\0' && !time(p, off)) {
                            out = WeekPlan{};
                            return false;
                        }
                    }

                    if (off == on) {
                        off = -1; // no length: on only
                    }

                    out.days[day][window].on = static_cast<uint16_t>(on);
                    out.days[day][window].off = off < 0 ? kNone : static_cast<uint16_t>(off);
                    ++window;
                }
            }

            /** The text for schedule.plan (at least kMaxText bytes) */
            void format(char* out, const size_t size) const {
                size_t n = 0;
                const auto put = [&](const char* t) {
                    while (*t != '\0' && n + 1 < size) {
                        out[n++] = *t++;
                    }
                };
                char buf[8];

                for (int d = 0; d < 7; ++d) {
                    bool first = true;

                    for (int w = 0; w < kWindows; ++w) {
                        const Window& x = days[d][w];

                        if (x.on == kNone) {
                            continue;
                        }

                        if (!first) {
                            put(",");
                        }

                        first = false;
                        snprintf(buf, sizeof(buf), "%02u:%02u", x.on / 60u, x.on % 60u);
                        put(buf);

                        if (x.off != kNone) {
                            snprintf(buf, sizeof(buf), "-%02u:%02u", x.off / 60u, x.off % 60u);
                            put(buf);
                        }
                    }

                    if (d < 6) {
                        put(";");
                    }
                }

                if (size > 0) {
                    out[n < size ? n : size - 1] = '\0';
                }
            }

            /** The plan from the settings before 09.10.2026: one window on the days of a bit mask (bit 0 = Monday) */
            static WeekPlan fromDays(const uint8_t daysMask, const uint16_t onMinute, const uint16_t offMinute) {
                WeekPlan p;

                for (int d = 0; d < 7; ++d) {
                    if ((daysMask & (1u << d)) != 0 && onMinute < 1440) {
                        p.days[d][0].on = onMinute;
                        p.days[d][0].off = offMinute < 1440 && offMinute != onMinute ? offMinute : kNone;
                    }
                }

                return p;
            }

        private:
            static bool time(const char*& p, int& minutes) {
                int h = 0, m = 0, digits = 0;

                while (*p >= '0' && *p <= '9' && digits < 2) {
                    h = h * 10 + (*p++ - '0');
                    ++digits;
                }

                if (digits == 0 || *p != ':') {
                    return false;
                }

                ++p;
                digits = 0;

                while (*p >= '0' && *p <= '9' && digits < 2) {
                    m = m * 10 + (*p++ - '0');
                    ++digits;
                }

                if (digits != 2 || h > 23 || m > 59) {
                    return false;
                }

                minutes = h * 60 + m;
                return true;
            }
    };

    /**
     * Heating schedule (machineCare.h): at a window's on time the controller goes on, at its off time the machine goes
     * into standby. Fires once at that minute, not when the clock first appears later or jumps over it.
     */
    class Schedule {
        public:
            enum Action : uint8_t {
                kNone = 0,
                kOn = 1,
                kOff = 2,
            };

            /**
             * @param weekday 0 = Monday ... 6 = Sunday (local time)
             * @param minute minute of the day, 0..1439 (local time)
             * @param paused holiday: no on and no off until the set day has passed
             */
            Action update(const bool enabled, const WeekPlan& plan, const int weekday, const int minute, const bool paused = false) {
                const int key = weekday * 1440 + minute;

                if (key == last_) {
                    return kNone;
                }

                const bool first = last_ < 0;
                last_ = key;

                if (first || !enabled || paused || weekday < 0 || weekday > 6 || minute < 0 || minute > 1439) {
                    return kNone;
                }

                const int yesterday = (weekday + 6) % 7;
                bool on = false, off = false;

                for (int w = 0; w < WeekPlan::kWindows; ++w) {
                    const WeekPlan::Window& x = plan.days[weekday][w];
                    const WeekPlan::Window& y = plan.days[yesterday][w];

                    on = on || (x.on != WeekPlan::kNone && x.on == minute);
                    off = off || (x.on != WeekPlan::kNone && x.off != WeekPlan::kNone && x.off > x.on && x.off == minute);
                    off = off || (y.on != WeekPlan::kNone && y.off != WeekPlan::kNone && y.off < y.on && y.off == minute); // over midnight
                }

                return on ? kOn : off ? kOff : kNone;
            }

        private:
            int last_ = -1;
    };

    /**
     * Standby: the brew switch wakes the machine only when it is turned on during the standby. One left on when the
     * standby began (after a shot, a backflush) does not, or the machine would wake right away again and again.
     * Called every loop while in standby; a gap of more than a second means the standby began anew.
     */
    class StandbyWake {
        public:
            static constexpr uint32_t kGapMs = 1000;

            /** @return true: wake now */
            bool update(const uint32_t nowMs, const bool switchOn) {
                if (!running_ || nowMs - lastMs_ > kGapMs) {
                    seenOff_ = false; // standby just began
                }

                running_ = true;
                lastMs_ = nowMs;

                if (!switchOn) {
                    seenOff_ = true;
                    return false;
                }

                if (!seenOff_) {
                    return false;
                }

                seenOff_ = false;
                return true;
            }

        private:
            uint32_t lastMs_ = 0;
            bool running_ = false;
            bool seenOff_ = false;
    };

    /**
     * Standby with the block kept warm (Dominik, 09.10.2026: "Temperatur wählbar, nach Zeit ganz aus"; Sanremo ECO,
     * Ascaso, Profitec Eco and the Lelit Bianca keep it warm too): a standby from the timer or the page's button heats
     * to a lower temperature for some hours, then the heater goes off. The schedule's off time is off at once
     * (coldNext()). Call update() once per loop.
     */
    class StandbyWarm {
        public:
            /** The standby beginning now (or the one running) without warming: the schedule's off time */
            void coldNext() {
                cold_ = true;
                active_ = false;
            }

            /**
             * @param celsius warming temperature, <= 0: heater off in standby (as before)
             * @param hours then off for good
             * @return true: heat to celsius now
             */
            bool update(const uint32_t nowMs, const bool inStandby, const float celsius, const float hours) {
                if (inStandby && !wasStandby_) {
                    active_ = !cold_ && celsius > 0.0f;
                    sinceMs_ = nowMs;
                }

                if (!inStandby || celsius <= 0.0f) {
                    active_ = false;
                }

                if (active_ && static_cast<float>(nowMs - sinceMs_) >= hours * 3600000.0f) {
                    active_ = false;
                    ended_ = true;
                }

                wasStandby_ = inStandby;
                cold_ = false;
                return active_;
            }

            bool active() const {
                return active_;
            }

            /** Minutes until the heater goes off (rounded up), -1 when not warming */
            int minutesLeft(const uint32_t nowMs, const float hours) const {
                if (!active_) {
                    return -1;
                }

                const float left = hours * 3600000.0f - static_cast<float>(nowMs - sinceMs_);
                return left <= 0.0f ? 0 : static_cast<int>((left + 59999.0f) / 60000.0f);
            }

            /** The warming time ran out since the last call (for a log line) */
            bool takeEnded() {
                const bool e = ended_;
                ended_ = false;
                return e;
            }

        private:
            uint32_t sinceMs_ = 0;
            bool active_ = false;
            bool wasStandby_ = false;
            bool cold_ = false;
            bool ended_ = false;
    };

    /**
     * Rinse after a shot without pre-infusion (Dominik, 09.10.2026; the Breville Dual Boiler and Lelit's LCC have a
     * flush without pre-infusion for this): while the page and the display ask to rinse, at most kWindowMs after the
     * shot, the brew switch runs the pump straight away. The run is the rinse throughout (Dominik, 09.10.2026: "nach
     * einem bezug wenn man nochmal s3 schaltet auch wirklich spülen dranstehen"): display and page say "Spülen", it is
     * never logged as a shot, no target ends it (its water on the scale would stop it by weight at once), only the
     * switch or kMaxMs ("10s max"). A second shot within the window without rinsing would be a rinse too.
     */
    struct RinseAfterShot {
            static constexpr uint32_t kWindowMs = 120000;
            static constexpr uint32_t kMaxMs = 10000;

            static bool expected(const bool rinsePending, const uint32_t nowMs, const uint32_t shotEndMs) {
                return rinsePending && nowMs - shotEndMs <= kWindowMs;
            }

            /** The rinse has run long enough: stop the pump */
            static bool over(const uint32_t runMs) {
                return runMs >= kMaxMs;
            }
    };

    /**
     * A switch input that must stay the other way for holdMs before it counts: the water level sensor through the
     * plastic of tank and tray flickered full/empty every 1-10 s (08.10.2026), which cut the warm-up flush short.
     * The first reading counts at once (no delay at the start).
     */
    class Debounce {
        public:
            explicit Debounce(const uint32_t holdMs) : holdMs_(holdMs) {}

            bool update(const bool in, const uint32_t nowMs) {
                if (!started_) {
                    started_ = true;
                    state_ = in;
                    return state_;
                }

                if (in == state_) {
                    pending_ = false;
                    return state_;
                }

                if (!pending_) {
                    pending_ = true;
                    since_ = nowMs;
                }

                if (nowMs - since_ >= holdMs_) {
                    state_ = in;
                    pending_ = false;
                }

                return state_;
            }

        private:
            uint32_t holdMs_;
            uint32_t since_ = 0;
            bool started_ = false;
            bool pending_ = false;
            bool state_ = false;
    };

    /**
     * How much water went through the thermoblock, for the descaling reminder. An estimate: a shot is what reached the
     * cup plus what the puck kept (about its dose again), a rinse runs at about 8 ml/s through the open group (cottec,
     * Kaffee-Netz t165325: ~10 ml/s or more for an empty shot, less through the Orione's group), a backflush cycle
     * fills the blind basket for a few seconds at ~2 ml/s.
     */
    struct Water {
            static constexpr float kRinseMlPerSecond = 8.0f;
            static constexpr float kShotMlPerSecond = 2.0f; // a shot without a scale
            static constexpr float kBackflushMlPerSecond = 2.0f;
            static constexpr uint32_t kShotDripMl = 15; // the 3-way valve lets the group's water off into the tray (own estimate)

            static uint32_t shotMl(const float grams, const float dose, const float seconds) {
                if (grams >= 0.0f && std::isfinite(grams)) {
                    return static_cast<uint32_t>(grams + (dose > 0.0f && std::isfinite(dose) ? dose : 0.0f) + 0.5f);
                }

                return seconds > 0.0f && std::isfinite(seconds) ? static_cast<uint32_t>(seconds * kShotMlPerSecond + 0.5f) : 0;
            }

            static uint32_t rinseMl(const float seconds) {
                return seconds > 0.0f && std::isfinite(seconds) ? static_cast<uint32_t>(seconds * kRinseMlPerSecond + 0.5f) : 0;
            }

            static uint32_t backflushMl(const int cycles, const float fillSeconds) {
                return cycles > 0 && fillSeconds > 0.0f && std::isfinite(fillSeconds) ? static_cast<uint32_t>(cycles * fillSeconds * kBackflushMlPerSecond + 0.5f) : 0;
            }
    };

    /** Counters as professional machines keep them, saved in NVS: shots today, this week, in all, coffee, water */
    class MachineStats {
        public:
            /** Days since 1970-01-01 of a local time; weeks start on Monday (1970-01-01 was a Thursday) */
            static int32_t dayOf(const int64_t localSeconds) {
                return static_cast<int32_t>(localSeconds >= 0 ? localSeconds / 86400 : (localSeconds - 86399) / 86400);
            }

            static int32_t weekOf(const int32_t day) {
                const int32_t shifted = day + 3; // Monday of the week 1969-12-29 is day -3
                return shifted >= 0 ? shifted / 7 : (shifted - 6) / 7;
            }

            /** A shot: dose in g (0: not given), water through the block in ml; day from dayOf() (< 0: clock not set) */
            void shot(const float dose, const uint32_t waterMl, const int32_t day) {
                ++total_;
                doseDeci_ += dose > 0.0f && std::isfinite(dose) ? static_cast<uint32_t>(dose * 10.0f + 0.5f) : 0;
                water(waterMl);

                if (day >= 0) {
                    roll(day);
                    ++today_;
                    ++week_;
                }
            }

            void water(const uint32_t ml) {
                waterMl_ += ml;
            }

            /** Water into the drip tray (rinses, backflush, the valve after a shot) */
            void drip(const uint32_t ml) {
                dripMl_ += ml;
            }

            void dripEmptied() {
                dripMl_ = 0;
            }

            uint32_t dripMl() const {
                return dripMl_;
            }

            /** Drip tray to empty: capacity in ml (0: no reminder), due at 80 % */
            bool dripDue(const float capacityMl) const {
                return capacityMl > 0.0f && dripMl_ >= capacityMl * 0.8f;
            }

            void descaled(const uint32_t whenUtc) {
                waterMl_ = 0;
                descaledAt_ = whenUtc;
            }

            /** All backflush cycles ran (0: clock not set, the date stays as it was) */
            void backflushed(const uint32_t whenUtc) {
                if (whenUtc != 0) {
                    backflushAt_ = whenUtc;
                }
            }

            uint32_t backflushAt() const {
                return backflushAt_;
            }

            uint32_t total() const {
                return total_;
            }

            float doseGrams() const {
                return doseDeci_ / 10.0f;
            }

            uint32_t waterMl() const {
                return waterMl_;
            }

            uint32_t descaledAt() const {
                return descaledAt_;
            }

            uint16_t today(const int32_t day) const {
                return day >= 0 && day == day_ ? today_ : 0;
            }

            uint16_t week(const int32_t day) const {
                return day >= 0 && weekOf(day) == weekNo_ ? week_ : 0;
            }

            /** Descaling due: limit in litres (0: no reminder) */
            bool descaleDue(const float limitLitres) const {
                return limitLitres > 0.0f && waterMl_ >= limitLitres * 1000.0f;
            }

            struct Stored {
                    uint8_t version;
                    uint32_t total;
                    uint32_t doseDeci;
                    uint32_t waterMl;
                    uint32_t descaledAt;
                    int32_t day;
                    int32_t weekNo;
                    uint16_t today;
                    uint16_t week;
                    uint32_t backflushAt; // 2
                    uint32_t dripMl;      // 3
            };

            /** Format 2 (09.10.2026, before the drip tray) */
            struct StoredV2 {
                    uint8_t version;
                    uint32_t total;
                    uint32_t doseDeci;
                    uint32_t waterMl;
                    uint32_t descaledAt;
                    int32_t day;
                    int32_t weekNo;
                    uint16_t today;
                    uint16_t week;
                    uint32_t backflushAt;
            };

            /** Format 1 (08.10.2026, before the backflush date): read once, saved as 2 */
            struct StoredV1 {
                    uint8_t version;
                    uint32_t total;
                    uint32_t doseDeci;
                    uint32_t waterMl;
                    uint32_t descaledAt;
                    int32_t day;
                    int32_t weekNo;
                    uint16_t today;
                    uint16_t week;
            };

            static constexpr size_t kMaxStoredSize = sizeof(Stored);

            static bool readable(const size_t length) {
                return length == sizeof(Stored) || length == sizeof(StoredV2) || length == sizeof(StoredV1);
            }

            Stored stored() const {
                return Stored{kVersion, total_, doseDeci_, waterMl_, descaledAt_, day_, weekNo_, today_, week_, backflushAt_, dripMl_};
            }

            bool restore(const void* data, const size_t length) {
                Stored s{};

                if (data != nullptr && length == sizeof(StoredV1) && static_cast<const uint8_t*>(data)[0] == 1) {
                    StoredV1 o{};
                    std::memcpy(&o, data, sizeof(o));
                    s = Stored{kVersion, o.total, o.doseDeci, o.waterMl, o.descaledAt, o.day, o.weekNo, o.today, o.week, 0, 0};
                }
                else if (data != nullptr && length == sizeof(StoredV2) && static_cast<const uint8_t*>(data)[0] == 2) {
                    StoredV2 o{};
                    std::memcpy(&o, data, sizeof(o));
                    s = Stored{kVersion, o.total, o.doseDeci, o.waterMl, o.descaledAt, o.day, o.weekNo, o.today, o.week, o.backflushAt, 0};
                }
                else if (data == nullptr || length != sizeof(s)) {
                    return false;
                }
                else {
                    std::memcpy(&s, data, sizeof(s));
                }

                if (s.version != kVersion) {
                    return false;
                }

                total_ = s.total;
                doseDeci_ = s.doseDeci;
                waterMl_ = s.waterMl;
                descaledAt_ = s.descaledAt;
                day_ = s.day;
                weekNo_ = s.weekNo;
                today_ = s.today;
                week_ = s.week;
                backflushAt_ = s.backflushAt;
                dripMl_ = s.dripMl;
                return true;
            }

        private:
            void roll(const int32_t day) {
                if (day != day_) {
                    day_ = day;
                    today_ = 0;
                }

                if (weekOf(day) != weekNo_) {
                    weekNo_ = weekOf(day);
                    week_ = 0;
                }
            }

            static constexpr uint8_t kVersion = 3; // 2: date of the last backflush, 3: water in the drip tray

            uint32_t total_ = 0;
            uint32_t doseDeci_ = 0;
            uint32_t waterMl_ = 0;
            uint32_t descaledAt_ = 0;
            int32_t day_ = -1;
            int32_t weekNo_ = -1;
            uint16_t today_ = 0;
            uint16_t week_ = 0;
            uint32_t backflushAt_ = 0;
            uint32_t dripMl_ = 0;
    };

    /**
     * Cleaning with detergent, as professional machines run it: the backflush cycles with detergent, then the
     * machine asks to rinse the blind basket out, then the same cycles again with clear water.
     */
    class CleaningProgram {
        public:
            enum Phase : uint8_t {
                kOff = 0,
                kDetergent = 1, // the cycles with detergent run (or are about to)
                kRinseOut = 2,  // done with detergent: rinse the blind basket out, then the brew switch off and on again
                kRinse = 3,     // the cycles with clear water run
            };

            /** The user chose cleaning with detergent */
            void start() {
                phase_ = kDetergent;
            }

            void cancel() {
                phase_ = kOff;
            }

            /**
             * All backflush cycles ran
             * @return true: the program is over (backflush mode may go off); false: it goes on with rinsing
             */
            bool cyclesDone() {
                if (phase_ == kDetergent) {
                    phase_ = kRinseOut;
                    return false;
                }

                phase_ = kOff;
                return true;
            }

            /** The backflush cycles start (brew switch on in backflush mode) */
            void cyclesStart() {
                if (phase_ == kRinseOut) {
                    phase_ = kRinse;
                }
            }

            Phase phase() const {
                return phase_;
            }

        private:
            Phase phase_ = kOff;
    };

    /**
     * Temperature course during the shot: the setpoint moves in a straight line from the set brew temperature to it
     * plus endDelta over the shot (e.g. 94 -> 91 °C, as declining profiles on the Decent). The thermoblock follows the
     * setpoint only with a few seconds' delay.
     */
    struct TempProfile {
            static constexpr double kMaxDelta = 5.0;

            static double setpoint(const double base, const double endDelta, const double elapsedSeconds, const double durationSeconds) {
                if (!std::isfinite(endDelta) || endDelta == 0.0 || !std::isfinite(elapsedSeconds) || !(durationSeconds > 0.0)) {
                    return base;
                }

                const double d = endDelta > kMaxDelta ? kMaxDelta : endDelta < -kMaxDelta ? -kMaxDelta : endDelta;
                const double share = elapsedSeconds <= 0.0 ? 0.0 : elapsedSeconds >= durationSeconds ? 1.0 : elapsedSeconds / durationSeconds;
                return base + d * share;
            }
    };

    /**
     * Channeling: water finds a path through the puck and the flow jumps up while it should rise slowly or stay. Looks
     * at the flow (g/s) from the scale, one value every intervalSeconds, from the first drops until the pump stopped:
     * a value at least kJump g/s and kFactor times over the median of the preceding kWindow values, for kHold values
     * in a row. The first kSkip seconds after the first drops are left out (the flow builds up there).
     */
    struct ChannelCheck {
            static constexpr float kJump = 0.8f;
            static constexpr float kFactor = 1.5f;
            static constexpr float kMinBase = 0.3f;
            static constexpr int kWindow = 4;
            static constexpr int kHold = 2;
            static constexpr float kSkip = 3.0f;

            /** @return seconds since the start when the jump began, < 0 if none */
            static float find(const float* flow, const int count, const float intervalSeconds, const float firstDropSeconds) {
                if (flow == nullptr || count <= 0 || !(intervalSeconds > 0.0f) || !(firstDropSeconds >= 0.0f)) {
                    return -1.0f;
                }

                const int from = static_cast<int>((firstDropSeconds + kSkip) / intervalSeconds + 0.5f);
                int run = 0;

                for (int i = from < kWindow ? kWindow : from; i < count; ++i) {
                    float w[kWindow];
                    bool valid = true;

                    for (int k = 0; k < kWindow; ++k) {
                        w[k] = flow[i - kWindow + k];
                        valid = valid && std::isfinite(w[k]) && w[k] >= 0.0f;
                    }

                    if (!valid || !std::isfinite(flow[i]) || flow[i] < 0.0f) {
                        run = 0;
                        continue;
                    }

                    // median of four: sort them
                    for (int a = 1; a < kWindow; ++a) {
                        for (int b = a; b > 0 && w[b - 1] > w[b]; --b) {
                            const float t = w[b];
                            w[b] = w[b - 1];
                            w[b - 1] = t;
                        }
                    }

                    const float base = (w[kWindow / 2 - 1] + w[kWindow / 2]) * 0.5f;
                    const bool jump = base >= kMinBase && flow[i] >= base + kJump && flow[i] >= base * kFactor;
                    run = jump ? run + 1 : 0;

                    if (run >= kHold) {
                        return (i - kHold + 1) * intervalSeconds;
                    }
                }

                return -1.0f;
            }
    };

} // namespace orione
