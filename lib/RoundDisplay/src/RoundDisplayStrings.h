/**
 * @file RoundDisplayStrings.h
 *
 * @brief Texts of the round display UI in German and English.
 *
 * Texts in capitals are drawn with the label font, the others with the text font
 * (the tests check that both fonts contain every character used here).
 */

#pragma once

#include "RoundDisplayModel.h"

#include <cstddef>

namespace rd {

    struct Strings {
            const char* heatingUp;
            const char* ready;
            const char* heating;
            const char* cooling;
            const char* brew;
            const char* preinfusion;
            const char* pause;
            const char* done;
            const char* flush;
            const char* hotWater;
            const char* steam;
            const char* backflush;
            const char* filling;
            const char* flushing;
            const char* waterTank;
            const char* empty;
            const char* refill;
            const char* standby;
            const char* pidOff;
            const char* pidOffHint;
            const char* sensorError;
            const char* overTemp;
            const char* heaterOff;
            const char* checkSensor;
            const char* heaterOffUntil;
            const char* setpoint;
            const char* target;
            const char* switchOn;
            const char* switchOff;
            const char* toStart;
            const char* toFinish;
            const char* scaleFault;
            const char* noWifi;
            const char* scaleDisconnected;
    };

    inline constexpr Strings kGerman = {
        "HEIZT AUF",
        "BEREIT",
        "HEIZT",
        "KÜHLT AB",
        "BEZUG",
        "PRE-INFUSION",
        "PAUSE",
        "FERTIG",
        "SPÜLEN",
        "HEISSWASSER",
        "DAMPF",
        "RÜCKSPÜLEN",
        "Füllen",
        "Spülen",
        "WASSERTANK",
        "LEER",
        "Bitte nachfüllen",
        "STANDBY",
        "PID AUS",
        "Manuell deaktiviert",
        "SENSORFEHLER",
        "ÜBERTEMPERATUR",
        "Heizung aus",
        "Fühler prüfen",
        "bis unter",
        "Soll",
        "Ziel",
        "Bezugsschalter an",
        "Bezugsschalter aus",
        "zum Starten",
        "zum Beenden",
        "Waage gestört",
        "Kein WLAN",
        "Waage getrennt",
    };

    inline constexpr Strings kEnglish = {
        "HEATING UP",
        "READY",
        "HEATING",
        "COOLING",
        "BREW",
        "PRE-INFUSION",
        "PAUSE",
        "DONE",
        "FLUSH",
        "HOT WATER",
        "STEAM",
        "BACKFLUSH",
        "Filling",
        "Flushing",
        "WATER TANK",
        "EMPTY",
        "Please refill",
        "STANDBY",
        "PID OFF",
        "Disabled manually",
        "SENSOR ERROR",
        "OVERTEMPERATURE",
        "Heater off",
        "Check sensor",
        "until below",
        "Set",
        "Target",
        "Brew switch on",
        "Brew switch off",
        "to start",
        "to finish",
        "Scale fault",
        "No WiFi",
        "Scale not connected",
    };

    inline const Strings& strings(const Language lang) {
        return lang == Language::German ? kGerman : kEnglish;
    }

    /** Number of texts in Strings; the struct holds nothing but text pointers */
    constexpr size_t kStringCount = sizeof(Strings) / sizeof(const char*);
    static_assert(sizeof(Strings) == kStringCount * sizeof(const char*), "Strings must only hold text pointers");

    /** i-th text of a table, for checks that walk over all of them */
    inline const char* stringAt(const Strings& table, const size_t i) {
        return reinterpret_cast<const char* const*>(&table)[i];
    }

} // namespace rd
