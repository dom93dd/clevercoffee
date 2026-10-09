/**
 * @file beanProfiles.h
 *
 * @brief Orione build (CC_ORIONE): a recipe per bean (lib/Orione/src/OrioneBeans.h). The beans are
 *        the name typed in brew.beans; when it changes, the old bean keeps its dose, grind setting,
 *        target weight, brew temperature and learned lag, and a bean seen before gets its own back.
 *        A new one starts from the current values. Changes to the current bean's values (on the
 *        page, the lag learned after a shot) are taken along. Kept in NVS next to the shots
 *        (~520 bytes), written when the bean changes and 10 s after the last change of a value,
 *        never during a shot. GET /beans for the page, POST /beans/delete to drop one.
 */

#pragma once

#include "shotHistory.h"
#include <OrioneBeans.h>
#include <Preferences.h>
#include <memory>

namespace bean_profiles {

    inline orione::BeanProfiles profiles;
    inline orione::BeanRecipe live; // the current bean's values as last seen
    inline portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED; // profiles and live are read by the web server

    inline constexpr const char* kNamespace = shot_history::kNamespace;
    inline constexpr const char* kKey = "beans";
    inline constexpr unsigned long kCheckMs = 500;
    inline constexpr unsigned long kSaveAfterMs = 10000;

    inline unsigned long lastCheck = 0;
    inline unsigned long changedAt = 0;
    inline bool unsaved = false;
    inline volatile bool saveRequested = false; // a bean deleted on the page

    inline uint16_t tenths(const double v, const double max) {
        const double t = v * 10.0;
        return std::isfinite(t) && t > 0.0 && t < max ? static_cast<uint16_t>(t + 0.5) : 0;
    }

    inline orione::BeanRecipe fromConfig() {
        orione::BeanRecipe r;
        snprintf(r.name, sizeof(r.name), "%s", config.get<String>("brew.beans").c_str());
        r.doseTenths = tenths(config.get<double>("brew.dose"), 65535.0);
        snprintf(r.grind, sizeof(r.grind), "%s", config.get<String>("brew.grind").c_str());
        r.targetTenths = tenths(config.get<double>("brew.by_weight.target_weight"), 65535.0);
        r.setpointTenths = static_cast<int16_t>(tenths(config.get<double>("brew.setpoint"), 32767.0));
        const double lag = config.get<double>("brew.by_weight.lag") * 100.0;
        r.lagCs = std::isfinite(lag) && lag > 0.0 && lag < 65535.0 ? static_cast<uint16_t>(lag + 0.5) : 0;
        return r;
    }

    inline bool sameValues(const orione::BeanRecipe& a, const orione::BeanRecipe& b) {
        return strcmp(a.name, b.name) == 0 && a.doseTenths == b.doseTenths && strcmp(a.grind, b.grind) == 0 && a.targetTenths == b.targetTenths &&
               a.setpointTenths == b.setpointTenths && a.lagCs == b.lagCs;
    }

    /** The bean's values into the settings; what it does not know (0) stays as it is */
    inline void apply(const orione::BeanRecipe& r) {
        auto& registry = ParameterRegistry::getInstance();

        if (r.doseTenths) {
            registry.setParameterValue("brew.dose", r.doseTenths / 10.0);
        }

        registry.setParameterValue<String>("brew.grind", String(r.grind));

        if (r.targetTenths) {
            registry.setParameterValue("brew.by_weight.target_weight", r.targetTenths / 10.0);
        }

        if (r.setpointTenths > 0) {
            registry.setParameterValue("brew.setpoint", r.setpointTenths / 10.0);
        }

        registry.setParameterValue("brew.by_weight.lag", r.lagCs / 100.0);
        registry.forceSave();
    }

    inline void save() {
        Preferences prefs;

        if (!prefs.begin(kNamespace, false)) {
            LOG(ERROR, "Beans: NVS not available");
            return;
        }

        auto stored = std::make_unique<orione::BeanProfiles::Stored>();
        portENTER_CRITICAL(&lock);
        *stored = profiles.stored();
        portEXIT_CRITICAL(&lock);
        prefs.putBytes(kKey, stored.get(), sizeof(*stored));
        prefs.end();
        unsaved = false;
    }

    inline void begin() {
        Preferences prefs;

        if (prefs.begin(kNamespace, true)) {
            auto saved = std::make_unique<uint8_t[]>(orione::BeanProfiles::kMaxStoredSize); // this format or the one before
            const size_t length = prefs.getBytesLength(kKey);

            if (orione::BeanProfiles::readable(length) && prefs.getBytes(kKey, saved.get(), length) == length) {
                profiles.restore(saved.get(), length);
            }

            prefs.end();
        }

        live = fromConfig();

        if (!orione::BeanProfiles::blank(live.name) && profiles.find(live.name) < 0) {
            profiles.put(live); // the bean of the first start with profiles, or one forgotten
            save();
        }

        LOGF(INFO, "Beans: %d kept, now \"%s\"", profiles.count(), live.name);
    }

    /**
     * Call from loop()
     * @param brewing a shot or backflush runs: no NVS write now
     */
    inline void loop(const bool brewing) {
        if (millis() - lastCheck < kCheckMs) {
            return;
        }

        lastCheck = millis();
        orione::BeanRecipe now = fromConfig();

        if (!orione::BeanProfiles::sameName(now.name, live.name) && !(orione::BeanProfiles::blank(now.name) && orione::BeanProfiles::blank(live.name))) {
            // another bean: the old one keeps its values as they were before the name changed
            portENTER_CRITICAL(&lock);

            if (!orione::BeanProfiles::blank(live.name)) {
                profiles.put(live);
            }

            const int i = profiles.find(now.name);
            const bool known = i >= 0;

            if (known) {
                const char* typed = now.name;
                orione::BeanRecipe back = profiles.at(i);
                snprintf(back.name, sizeof(back.name), "%s", typed); // as typed this time
                now = back;
            }

            if (!orione::BeanProfiles::blank(now.name)) {
                profiles.put(now);
            }

            live = now;
            portEXIT_CRITICAL(&lock);

            if (known) {
                apply(now);
                const orione::BeanRecipe held = fromConfig(); // as the settings hold it now (limits, rounding)
                portENTER_CRITICAL(&lock);
                live = held;
                portEXIT_CRITICAL(&lock);
                LOGF(INFO, "Beans: \"%s\" back, dose %.1f g, grind %s, target %.1f g, %.1f °C, lag %.2f s", now.name, now.doseTenths / 10.0, now.grind,
                     now.targetTenths / 10.0, now.setpointTenths / 10.0, now.lagCs / 100.0);
            }
            else if (!orione::BeanProfiles::blank(now.name)) {
                LOGF(INFO, "Beans: \"%s\" new, from the current values", now.name);
            }

            save();
            return;
        }

        if (!sameValues(now, live)) { // a value of the current bean changed (or the name's spelling)
            portENTER_CRITICAL(&lock);
            live = now;
            portEXIT_CRITICAL(&lock);

            if (!orione::BeanProfiles::blank(now.name)) {
                unsaved = true;
                changedAt = millis();
            }
        }

        if ((unsaved && millis() - changedAt >= kSaveAfterMs) || saveRequested) {
            if (brewing) {
                return;
            }

            saveRequested = false;

            if (!orione::BeanProfiles::blank(live.name)) {
                portENTER_CRITICAL(&lock);
                profiles.put(live);
                portEXIT_CRITICAL(&lock);
            }

            save();
        }
    }

    /** From the web server: the roast date of a bean (days since 1970-01-01, 0 none) */
    inline bool setRoast(const char* name, const uint16_t day) {
        portENTER_CRITICAL(&lock);
        const bool ok = profiles.setRoast(name, day);
        portEXIT_CRITICAL(&lock);

        if (ok) {
            saveRequested = true;
        }

        return ok;
    }

    /** From the web server: drop a bean that is not the current one */
    inline bool remove(const char* name) {
        portENTER_CRITICAL(&lock);
        const bool ok = !orione::BeanProfiles::sameName(name, live.name) && profiles.remove(name);
        portEXIT_CRITICAL(&lock);

        if (ok) {
            saveRequested = true;
        }

        return ok;
    }

    inline void printTenths(Print& out, const unsigned v) {
        v ? (void)out.printf("%u.%u", v / 10u, v % 10u) : (void)out.print("null");
    }

    /**
     * {"now":"current beans","beans":[{"n":"name","d":16.5,"m":"21","tw":33.0,"t":93.0,"lg":1.00,"rd":20365},...]}, last used first;
     * d, tw, t null if not known, rd the roast date in days since 1970-01-01 or null; the current bean with the values of the settings
     */
    inline void writeJson(Print& out) {
        auto copy = std::make_unique<orione::BeanProfiles>();
        auto current = std::make_unique<orione::BeanRecipe>();
        portENTER_CRITICAL(&lock);
        *copy = profiles;
        *current = live;
        portEXIT_CRITICAL(&lock);

        int order[orione::BeanProfiles::kSize];
        const int n = copy->byUse(order);
        out.print(R"({"now":")");
        shot_history::printJsonText(out, current->name);
        out.print(R"(","beans":[)");

        for (int k = 0; k < n; ++k) {
            // the current bean as it is now: its entry is only brought up to date when it is saved
            const auto& b = orione::BeanProfiles::sameName(copy->at(order[k]).name, current->name) ? *current : copy->at(order[k]);
            out.print(k ? R"(,{"n":")" : R"({"n":")");
            shot_history::printJsonText(out, b.name);
            out.print(R"(","d":)");
            printTenths(out, b.doseTenths);
            out.print(R"(,"m":")");
            shot_history::printJsonText(out, b.grind);
            out.print(R"(","tw":)");
            printTenths(out, b.targetTenths);
            out.print(R"(,"t":)");
            printTenths(out, b.setpointTenths > 0 ? static_cast<unsigned>(b.setpointTenths) : 0u);
            const uint16_t roast = copy->at(order[k]).roastDay; // kept with the bean, not in the settings
            out.printf(R"(,"lg":%u.%02u,"rd":)", b.lagCs / 100u, b.lagCs % 100u);
            roast ? (void)out.printf("%u}", static_cast<unsigned>(roast)) : (void)out.print("null}");
        }

        out.print("]}");
    }

} // namespace bean_profiles
