/**
 * @file shotHistory.h
 *
 * @brief Orione build (CC_ORIONE): the last five shots for the web interface ("Letzte Bezüge",
 *        GET /shots) and their curves (GET /shot?i=0..4: weight and brew temperature every
 *        0.5 s). Log and curves (lib/Orione/OrioneShots.h) live in NVS, so they survive switching
 *        the machine off; they are written once per shot, after the drops are counted. RAM: the
 *        curve being recorded (~400 bytes). The time of a shot comes from NTP as UTC, the
 *        browser shows it in its own time zone.
 */

#pragma once

#include <OrioneShots.h>
#include <Preferences.h>
#include <memory>
#include <time.h>

namespace shot_history {

    inline orione::ShotLog shotLog;
    inline orione::ShotCurve shotCurve; // the shot being recorded (or the last one, until saved)

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

    inline void curveKey(char (&key)[4], const int seq) {
        snprintf(key, sizeof(key), "c%d", seq % orione::ShotLog::kSize);
    }

    /** The log, and the curve of its newest shot */
    inline void save() {
        shotCurve.end();
        Preferences prefs;

        if (!prefs.begin(kNamespace, false)) {
            return;
        }

        const auto log = shotLog.stored();
        prefs.putBytes(kKey, &log, sizeof(log));
        const auto curve = shotCurve.stored();
        char key[4];
        curveKey(key, shotLog.at(0).seq);
        prefs.putBytes(key, &curve, sizeof(curve));
        prefs.end();
    }

    inline void brewStarted() {
        if (shotLog.settleNow()) {
            save(); // the previous shot was still counting drops
        }

        shotCurve.begin(millis());
    }

    /** @param grams brew weight, < 0 without a connected scale */
    inline void brewEnded(const double seconds, const float grams) {
        shotCurve.stopped();

        if (shotLog.record(static_cast<float>(seconds), grams, nowUtc(), millis())) {
            LOGF(INFO, "Shot logged: %.1f s, %.1f g", seconds, grams);
        }
        else {
            shotCurve.end(); // not a shot: no curve either
        }
    }

    /**
     * @param grams current brew weight (drops after the stop), < 0 without a connected scale
     * @param celsius brew temperature as the web interface shows it
     */
    inline void loop(const float grams, const double celsius) {
        shotCurve.sample(millis(), grams, static_cast<float>(celsius));

        if (shotLog.settle(millis(), grams)) {
            save();
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

    /**
     * Curve of shot i (0 = newest) as {"dt":ms,"stop":point,"w":[tenths of a gram] or null,"t":[tenths of a degree]}
     * @return false if there is no such shot or no curve saved for it
     */
    inline bool writeCurveJson(const int i, Print& out) {
        if (i < 0 || i >= shotLog.count()) {
            return false;
        }

        // ~800 bytes for a moment: on the heap, not on the web server task's small stack
        const auto c = std::make_unique<orione::ShotCurve>();
        auto& curve = *c;

        if (i == 0 && shotLog.settling()) {
            curve = shotCurve; // the newest one is not saved yet
        }
        else {
            Preferences prefs;

            if (!prefs.begin(kNamespace, true)) {
                return false;
            }

            const auto saved = std::make_unique<orione::ShotCurve::Stored>();
            char key[4];
            curveKey(key, shotLog.at(i).seq);
            const bool ok = prefs.getBytesLength(key) == sizeof(*saved) && prefs.getBytes(key, saved.get(), sizeof(*saved)) == sizeof(*saved) && curve.restore(saved.get(), sizeof(*saved));
            prefs.end();

            if (!ok) {
                return false;
            }
        }

        bool scale = false;

        for (int k = 0; k < curve.count() && !scale; ++k) {
            scale = curve.at(k).grams != orione::ShotCurve::kNone;
        }

        out.printf(R"({"dt":%u,"stop":%d,"w":)", static_cast<unsigned>(curve.intervalMs()), curve.stop());

        for (int series = 0; series < 2; ++series) {
            if (series == 0 && !scale) {
                out.print("null");
            }
            else {
                for (int k = 0; k < curve.count(); ++k) {
                    const int16_t v = series == 0 ? curve.at(k).grams : curve.at(k).celsius;
                    out.print(k ? "," : "[");

                    if (v == orione::ShotCurve::kNone) {
                        out.print("null");
                    }
                    else {
                        out.print(v);
                    }
                }

                out.print(curve.count() ? "]" : "[]");
            }

            out.print(series == 0 ? R"(,"t":)" : "}");
        }

        return true;
    }

} // namespace shot_history
