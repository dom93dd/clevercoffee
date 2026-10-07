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

#include <OrioneFlow.h>
#include <OrioneShots.h>
#include <Preferences.h>
#include <memory>
#include <time.h>

namespace shot_history {

    inline orione::ShotLog shotLog;
    inline orione::ShotCurve shotCurve; // the shot being recorded (or the last one, until saved)
    inline orione::FlowMeter flowMeter; // flow and first drops from the scale, at its own rate
    inline float startCelsius = 0;      // brew temperature when the shot started

    inline constexpr const char* kNamespace = "orione";
    inline constexpr const char* kKey = "shots";

    inline void begin() {
        Preferences prefs;

        if (!prefs.begin(kNamespace, true)) {
            return; // nothing saved yet
        }

        // this format or the one before (taken over after an update): ~420 bytes, setup() has room for it
        alignas(orione::ShotLog::Stored) uint8_t saved[orione::ShotLog::kMaxStoredSize];
        const size_t length = prefs.getBytesLength(kKey);

        if (orione::ShotLog::readable(length) && prefs.getBytes(kKey, saved, length) == length) {
            shotLog.restore(saved, length);
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
        flowMeter.stop();
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

    // A rating from the web page (its task) is applied and saved in loop(), where the log lives
    inline portMUX_TYPE rateLock = portMUX_INITIALIZER_UNLOCKED;
    inline int rateIndex = -1;
    inline uint8_t rateTaste = 0;

    inline void requestRating(const int i, const uint8_t taste) {
        portENTER_CRITICAL(&rateLock);
        rateIndex = i;
        rateTaste = taste;
        portEXIT_CRITICAL(&rateLock);
    }

    /** Shot i still the one the page means: its time (UTC) and seconds as the page has them */
    inline bool isShot(const int i, const uint32_t when, const float seconds) {
        return i >= 0 && i < shotLog.count() && shotLog.at(i).when == when && std::fabs(shotLog.at(i).seconds - seconds) < 0.06f;
    }

    // A delete from the web page, applied in loop() like a rating; checked there again (a shot may have come)
    inline int deleteIndex = -1;
    inline uint32_t deleteWhen = 0;
    inline float deleteSeconds = 0;

    inline void requestDelete(const int i, const uint32_t when, const float seconds) {
        portENTER_CRITICAL(&rateLock);
        deleteIndex = i;
        deleteWhen = when;
        deleteSeconds = seconds;
        portEXIT_CRITICAL(&rateLock);
    }

    /** Log only (the newest curve may still be recording) */
    inline void saveLog() {
        Preferences prefs;

        if (prefs.begin(kNamespace, false)) {
            const auto log = shotLog.stored();
            prefs.putBytes(kKey, &log, sizeof(log));
            prefs.end();
        }
    }

    inline void backflushDone() {
        shotLog.backflushDone();
        saveLog();
        LOG(INFO, "Backflush done: shot counter reset");
    }

    // brew by weight: what the pump stopped with (brewHandler.h), and what the lag is learned from once the drops are counted
    inline float stopLag = 0.0f;  // s
    inline float stopFlow = 0.0f; // g/s
    inline bool learnPending = false;
    inline float learnTarget = 0.0f;

    /** The pump stopped at the target weight minus lagSeconds of flow (g/s) */
    inline void noteStop(const float lagSeconds, const float flow) {
        stopLag = lagSeconds;
        stopFlow = flow;
    }

    inline void learnFromShot(); // below

    inline void brewStarted(const double celsius) {
        if (shotLog.settleNow()) {
            save(); // the previous shot was still counting drops
            learnFromShot();
        }

        startCelsius = static_cast<float>(celsius);
        noteStop(0.0f, 0.0f);
        shotCurve.begin(millis());
        flowMeter.start(millis());
    }

    /** g/s while a shot runs or its drops are counted, < 0 otherwise or without scale */
    inline float liveFlow(const bool scaleConnected) {
        return scaleConnected && flowMeter.running() ? flowMeter.flow() : -1.0f;
    }

    /** After the drops: correct the lag by what ended up in the cup (orione::BrewLag, after the shotStopper) */
    inline void learnFromShot() {
        if (!learnPending || shotLog.count() == 0) {
            return;
        }

        learnPending = false;
        const auto& x = shotLog.at(0);
        const float lag = x.lagCs / 100.0f, flow = x.flowStopCs / 100.0f;
        const float next = std::round(orione::BrewLag::learn(lag, learnTarget, x.grams, flow) * 100.0f) / 100.0f;
        LOGF(INFO, "Brew by weight: target %.1f g, %.1f g at the stop (%.1f g/s), %.1f g in the cup, lag %.2f -> %.2f s", learnTarget, x.stopTenths / 10.0f, flow, x.grams, lag, next);

        if (config.get<bool>("brew.by_weight.learn") && std::fabs(next - lag) >= 0.005f) {
            ParameterRegistry::getInstance().setParameterValue("brew.by_weight.lag", static_cast<double>(next));
        }
    }

    /**
     * @param grams in the cup when the pump stopped, < 0 without a connected scale
     * @param byWeight the shot stopped at its target weight (minus the lead, noteStop())
     */
    inline void brewEnded(const double seconds, const float grams, const bool byWeight) {
        shotCurve.stopped();

        if (shotLog.record(static_cast<float>(seconds), grams, nowUtc(), millis())) {
            shotLog.noteRecipe(config.get<float>("brew.dose"), config.get<String>("brew.grind").c_str(), config.get<String>("brew.beans").c_str());
            shotLog.noteFacts(startCelsius, grams < 0 ? -1.0f : flowMeter.firstDropSeconds());
            learnTarget = byWeight && grams >= 0 ? config.get<float>("brew.by_weight.target_weight") : 0.0f;
            learnPending = learnTarget > 0.0f;

            if (learnTarget > 0.0f) {
                shotLog.noteWeights(learnTarget, grams, orione::BrewLag::leadGrams(stopLag, stopFlow), stopLag, stopFlow);
            }
            else {
                shotLog.noteWeights(0.0f, grams, 0.0f);
            }

            LOGF(INFO, "Shot logged: %.1f s, %.1f g", seconds, grams);
        }
        else {
            shotCurve.end(); // not a shot: no curve either
            flowMeter.stop();
        }
    }

    /**
     * @param grams current brew weight (drops after the stop), < 0 without a connected scale
     * @param celsius brew temperature as the web interface shows it
     */
    inline void loop(const float grams, const double celsius) {
        if (grams >= 0) {
            flowMeter.add(millis(), grams);
        }

        shotCurve.sample(millis(), grams, static_cast<float>(celsius), grams >= 0 && flowMeter.running() ? flowMeter.flow() : -1.0f);

        if (rateIndex >= 0) {
            portENTER_CRITICAL(&rateLock);
            const int i = rateIndex;
            const uint8_t taste = rateTaste;
            rateIndex = -1;
            portEXIT_CRITICAL(&rateLock);

            if (shotLog.rate(i, taste) && !shotLog.settling()) {
                saveLog(); // while settling, the save after the drops takes it along
            }
        }

        if (deleteIndex >= 0) {
            portENTER_CRITICAL(&rateLock);
            const int i = deleteIndex;
            const uint32_t when = deleteWhen;
            const float seconds = deleteSeconds;
            deleteIndex = -1;
            portEXIT_CRITICAL(&rateLock);

            if (isShot(i, when, seconds) && shotLog.remove(i)) {
                if (i == 0) {
                    learnPending = false; // a deleted shot teaches the lag nothing
                }

                saveLog();
                LOGF(INFO, "Shot deleted: %.1f s", static_cast<double>(seconds));
            }
        }

        if (shotLog.settle(millis(), grams)) {
            save();
            learnFromShot();
        }
    }

    /**
     * {"now":UTC,"bf":shots since the last backflush,"shots":[{"s":25.3,"g":36.1,"at":UTC,"d":18.0,"m":"12","b":"beans","r":2,"t0":93.4,"fd":6.2},...]},
     * newest first; g null without scale, at 0 if unknown, d null if not given, r 0 not rated 1 sour 2 good 3 bitter,
     * t0 brew temperature at the start, fd seconds until the first drops (both null if unknown); brew by weight:
     * tw target, sw in the cup when the pump stopped (g holds what was there after the drops), ld the lead it stopped with,
     * lg that lead as seconds of flow, fs the flow when the pump stopped (g/s; both null if unknown)
     */
    /** Text typed by the user inside a JSON string: quotes and backslashes escaped, control characters left out */
    inline void printJsonText(Print& out, const char* text) {
        for (const char* c = text; *c != '\0'; ++c) {
            if (*c == '"' || *c == '\\') {
                out.print('\\');
            }

            if (static_cast<unsigned char>(*c) >= 0x20) {
                out.print(*c);
            }
        }
    }

    inline void writeJson(Print& out) {
        out.printf(R"({"now":%u,"bf":%u,"shots":[)", static_cast<unsigned>(nowUtc()), static_cast<unsigned>(shotLog.sinceBackflush()));

        for (int i = 0; i < shotLog.count(); ++i) {
            const auto& s = shotLog.at(i);
            out.printf(R"(%s{"s":%.1f,"g":)", i ? "," : "", static_cast<double>(s.seconds));

            if (s.grams < 0) {
                out.print("null");
            }
            else {
                out.printf("%.1f", static_cast<double>(s.grams));
            }

            out.printf(R"(,"at":%u,"d":)", static_cast<unsigned>(s.when));

            if (s.doseTenths == 0) {
                out.print("null");
            }
            else {
                out.printf("%u.%u", s.doseTenths / 10u, s.doseTenths % 10u);
            }

            out.print(R"(,"m":")");
            printJsonText(out, s.grind);
            out.print(R"(","b":")");
            printJsonText(out, s.beans);
            out.printf(R"(","r":%u,"t0":)", static_cast<unsigned>(s.taste));
            s.startTenths ? (void)out.printf("%d.%d", s.startTenths / 10, s.startTenths % 10) : (void)out.print("null");
            out.print(R"(,"fd":)");
            s.firstDropTenths ? (void)out.printf("%u.%u", s.firstDropTenths / 10u, s.firstDropTenths % 10u) : (void)out.print("null");
            out.print(R"(,"tw":)");
            s.targetTenths ? (void)out.printf("%u.%u", s.targetTenths / 10u, s.targetTenths % 10u) : (void)out.print("null");
            out.print(R"(,"sw":)");
            s.stopTenths ? (void)out.printf("%u.%u", s.stopTenths / 10u, s.stopTenths % 10u) : (void)out.print("null");
            out.printf(R"(,"ld":%u.%u,"lg":)", s.leadTenths / 10u, s.leadTenths % 10u);
            s.lagCs ? (void)out.printf("%u.%02u", s.lagCs / 100u, s.lagCs % 100u) : (void)out.print("null");
            out.print(R"(,"fs":)");
            s.flowStopCs ? (void)out.printf("%u.%02u", s.flowStopCs / 100u, s.flowStopCs % 100u) : (void)out.print("null");
            out.print("}");
        }

        out.print("]}");
    }

    /**
     * Curve of shot i (0 = newest) as {"dt":ms,"stop":point,"w":[tenths of a gram] or null,"t":[tenths of a degree],"f":[hundredths of g/s] or null}
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

        for (int series = 0; series < 3; ++series) {
            if (series != 1 && !scale) {
                out.print("null");
            }
            else {
                for (int k = 0; k < curve.count(); ++k) {
                    const int16_t v = series == 0 ? curve.at(k).grams : series == 1 ? curve.at(k).celsius : curve.at(k).flow;
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

            out.print(series == 0 ? R"(,"t":)" : series == 1 ? R"(,"f":)" : "}");
        }

        return true;
    }

} // namespace shot_history
