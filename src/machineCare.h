/**
 * @file machineCare.h
 *
 * @brief Orione build (CC_ORIONE): what professional machines do besides the shot (lib/Orione/src/OrioneCare.h):
 *        counters (shots today, this week, in all, coffee) and the water through the thermoblock since the last
 *        descaling, kept in NVS next to the shots; the cleaning program with detergent; the heating schedule; the
 *        temperature course during the shot. Local time: Germany (CET/CEST), set with the NTP clock.
 */

#pragma once

#include <OrioneCare.h>
#include <Preferences.h>
#include <time.h>

namespace care {

    inline constexpr const char* kNamespace = "orione"; // as the shots (shotHistory.h)
    inline constexpr const char* kKey = "care";
    inline constexpr const char* kTimeZone = "CET-1CEST,M3.5.0,M10.5.0/3";

    inline orione::MachineStats stats;
    inline orione::CleaningProgram cleaning;
    inline orione::Schedule schedule;
    inline portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED; // stats are read by the web server
    inline bool unsaved = false;
    inline unsigned long changedAt = 0;
    inline volatile bool cleaningRequested = false; // POST /toggleBackflush?cleaner=1, started in loop()

    /** Local time now; false while the clock is not set (no NTP yet) */
    inline bool localNow(struct tm& t) {
        const time_t now = time(nullptr);

        if (now < 1700000000) {
            return false;
        }

        localtime_r(&now, &t);
        return true;
    }

    /** Days since 1970-01-01 of the local date (for the counters per day and week); -1 without a clock */
    inline int32_t localDay() {
        struct tm t;

        if (!localNow(t)) {
            return -1;
        }

        // days from the civil date (Howard Hinnant's algorithm)
        int y = t.tm_year + 1900;
        const unsigned m = static_cast<unsigned>(t.tm_mon + 1), d = static_cast<unsigned>(t.tm_mday);
        y -= m <= 2;
        const int era = (y >= 0 ? y : y - 399) / 400;
        const unsigned yoe = static_cast<unsigned>(y - era * 400);
        const unsigned doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1;
        const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
        return era * 146097 + static_cast<int32_t>(doe) - 719468;
    }

    inline void begin() {
        Preferences prefs;

        if (prefs.begin(kNamespace, true)) {
            alignas(orione::MachineStats::Stored) uint8_t saved[orione::MachineStats::kMaxStoredSize];
            const size_t length = prefs.getBytesLength(kKey);

            if (orione::MachineStats::readable(length) && prefs.getBytes(kKey, saved, length) == length) {
                stats.restore(saved, length); // an older format is saved anew with the next change
            }

            prefs.end();
        }
    }

    inline void save() {
        portENTER_CRITICAL(&lock);
        const auto s = stats.stored();
        unsaved = false;
        portEXIT_CRITICAL(&lock);
        Preferences prefs;

        if (prefs.begin(kNamespace, false)) {
            prefs.putBytes(kKey, &s, sizeof(s));
            prefs.end();
        }
    }

    inline void changed() {
        unsaved = true;
        changedAt = millis();
    }

    /** A shot was logged (shotHistory.h) */
    inline void shot(const float dose, const float grams, const float seconds) {
        const int32_t day = localDay();
        portENTER_CRITICAL(&lock);
        stats.shot(dose, orione::Water::shotMl(grams, dose, seconds), day);
        stats.drip(orione::Water::kShotDripMl);
        portEXIT_CRITICAL(&lock);
        save(); // with the shot's own save, once per shot
    }

    /** Water through the open group: a rinse by the brew switch or the warm-up flush */
    inline void rinse(const float pumpSeconds) {
        portENTER_CRITICAL(&lock);
        stats.water(orione::Water::rinseMl(pumpSeconds));
        stats.drip(orione::Water::rinseMl(pumpSeconds)); // without the portafilter: into the tray
        portEXIT_CRITICAL(&lock);
        changed();
    }

    /** All backflush cycles ran: their water, and the date for the page */
    inline void backflushed(const int cycles, const float fillSeconds) {
        const time_t now = time(nullptr);
        portENTER_CRITICAL(&lock);
        stats.water(orione::Water::backflushMl(cycles, fillSeconds));
        stats.drip(orione::Water::backflushMl(cycles, fillSeconds));
        stats.backflushed(now > 1700000000 ? static_cast<uint32_t>(now) : 0);
        portEXIT_CRITICAL(&lock);
        changed();
    }

    inline void descaled(const uint32_t whenUtc) {
        portENTER_CRITICAL(&lock);
        stats.descaled(whenUtc);
        portEXIT_CRITICAL(&lock);
        changed();
    }

    inline void dripEmptied() {
        portENTER_CRITICAL(&lock);
        stats.dripEmptied();
        portEXIT_CRITICAL(&lock);
        changed();
    }

    /** Call from loop(): saves 10 s after the last change, never while the pump runs */
    inline void loop(const bool busy) {
        if (unsaved && !busy && millis() - changedAt > 10000) {
            save();
        }
    }

    /**
     * {"today":n,"week":n,"total":n,"coffee":g,"water":ml,"descaledAt":UTC or 0,"descaleL":limit,"due":bool,"standby":minutes until
     * standby or -1,"rssi":dBm,"backflushAt":UTC of the last complete backflush or 0,"drip":ml in the drip tray,"dripCap":its capacity,
     * "dripDue":bool}
     */
    inline void writeJson(Print& out, const float descaleLitres, const int standbyMinutes, const int rssi, const float dripCapacityMl) {
        const int32_t day = localDay();
        portENTER_CRITICAL(&lock);
        const orione::MachineStats s = stats;
        portEXIT_CRITICAL(&lock);
        out.printf(R"({"today":%u,"week":%u,"total":%u,"coffee":%.1f,"water":%u,"descaledAt":%u,"descaleL":%.0f,"due":%s,"standby":%d,"rssi":%d,"backflushAt":%u,"drip":%u,"dripCap":%.0f,"dripDue":%s})", s.today(day), s.week(day),
                   static_cast<unsigned>(s.total()), static_cast<double>(s.doseGrams()), static_cast<unsigned>(s.waterMl()), static_cast<unsigned>(s.descaledAt()),
                   static_cast<double>(descaleLitres), s.descaleDue(descaleLitres) ? "true" : "false", standbyMinutes, rssi, static_cast<unsigned>(s.backflushAt()),
                   static_cast<unsigned>(s.dripMl()), static_cast<double>(dripCapacityMl), s.dripDue(dripCapacityMl) ? "true" : "false");
    }

} // namespace care
