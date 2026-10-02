/**
 * @file shotHistory.h
 *
 * @brief Orione build (CC_ORIONE): the last five shots for the web interface ("Letzte Bezüge",
 *        GET /shots). The log (lib/Orione/OrioneShots.h) lives in NVS, so it survives switching
 *        the machine off; it is written once per shot, after the drops are counted. The time of
 *        a shot comes from NTP as UTC, the browser shows it in its own time zone.
 */

#pragma once

#include <OrioneShots.h>
#include <Preferences.h>
#include <time.h>

namespace shot_history {

    inline orione::ShotLog shotLog;

    inline constexpr const char* kNamespace = "orione";
    inline constexpr const char* kKey = "shots";

    inline void begin() {
        Preferences prefs;

        if (!prefs.begin(kNamespace, true)) {
            return; // nothing saved yet
        }

        orione::ShotLog::Stored saved{};

        if (prefs.getBytesLength(kKey) == sizeof(saved)) {
            prefs.getBytes(kKey, &saved, sizeof(saved));
            shotLog.restore(&saved, sizeof(saved));
        }

        prefs.end();
    }

    /** Starts the clock from NTP (in the background); call once WiFi is up */
    inline void startClock() {
        configTime(0, 0, "pool.ntp.org", "time.google.com");
    }

    /** UTC seconds, 0 while the clock is not set */
    inline uint32_t nowUtc() {
        const time_t t = time(nullptr);
        return t > 1700000000 ? static_cast<uint32_t>(t) : 0;
    }

    /** @param grams brew weight, < 0 without a connected scale */
    inline void brewEnded(const double seconds, const float grams) {
        if (shotLog.record(static_cast<float>(seconds), grams, nowUtc(), millis())) {
            LOGF(INFO, "Shot logged: %.1f s, %.1f g", seconds, grams);
        }
    }

    /** @param grams current brew weight (drops after the stop), < 0 without a connected scale */
    inline void loop(const float grams) {
        if (!shotLog.settle(millis(), grams)) {
            return;
        }

        Preferences prefs;

        if (prefs.begin(kNamespace, false)) {
            const auto saved = shotLog.stored();
            prefs.putBytes(kKey, &saved, sizeof(saved));
            prefs.end();
        }
    }

    /** {"now":UTC,"shots":[{"s":25.3,"g":36.1,"at":UTC},...]}, newest first; g null without scale, at 0 if unknown */
    inline void writeJson(Print& out) {
        out.printf(R"({"now":%u,"shots":[)", static_cast<unsigned>(nowUtc()));

        for (int i = 0; i < shotLog.count(); ++i) {
            const auto& s = shotLog.at(i);
            out.printf(R"(%s{"s":%.1f,"g":)", i ? "," : "", static_cast<double>(s.seconds));

            if (s.grams < 0) {
                out.print("null");
            }
            else {
                out.printf("%.1f", static_cast<double>(s.grams));
            }

            out.printf(R"(,"at":%u})", static_cast<unsigned>(s.when));
        }

        out.print("]}");
    }

} // namespace shot_history
