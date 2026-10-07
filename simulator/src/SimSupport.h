/**
 * @file SimSupport.h
 *
 * @brief Pieces shared by the simulator and its tests: the simulated panel (the UI renders its
 *        bands into a sprite) and the named scenarios used for screenshots, gallery and golden images.
 */

#pragma once

#include <LovyanGFX.hpp>
#include <RoundDisplayControl.h>
#include <RoundDisplayUi.h>

#include "FakeMachine.h"

#include <cstring>
#include <functional>
#include <vector>

namespace sim {

    constexpr int kDisplay = rd::RoundUi::kWidth;
    constexpr int kBandHeight = 40;
    constexpr const char* kDefaultBrand = "DOMS COFFEE";

    /** Runs a shot with the scale (brew by weight 36 g) up to the given second, feeding the UI like the loop does */
    inline void runScaleShot(FakeMachine& m, rd::RoundUi& ui, const float until) {
        m.scale = true;
        m.targetBrewTime = 0;
        m.settle();
        m.toggleBrewSwitch();

        for (float t = 0.0f; t < until; t += 0.1f) {
            m.step(0.1f);
            ui.update(m.model(), static_cast<uint32_t>(t * 1000.0f));
        }
    }

    // -------------------------------------------------------------------------------------------
    // Simulated panel: the UI renders its bands into this sprite

    struct Panel {
            lgfx::LGFX_Sprite screen;
            lgfx::LGFX_Sprite bands[2];

            explicit Panel(const int bandHeight = kBandHeight) {
                screen.setColorDepth(16);
                screen.createSprite(kDisplay, kDisplay);

                for (auto& b : bands) {
                    b.setColorDepth(16);
                    b.createSprite(kDisplay, bandHeight);
                }
            }

            void render(rd::RoundUi& ui, const uint32_t nowMs) {
                ui.render(bands, 2, nowMs, [this](lgfx::LGFX_Sprite& band, const int top) { band.pushSprite(&screen, 0, top); });
            }
    };

    struct Scenario {
            const char* name;
            const char* caption;
            std::function<void(FakeMachine&, rd::RoundUi&)> setup;
            uint32_t atMs = 0; // render time; > 0 shows a frame inside a transition
    };

    /** Time the last runMachine() call ended at; scenarios that simulate a while render there */
    inline uint32_t gSimulatedMs = 0;

    /** Runs the simulated machine for some seconds, feeding the UI every 100 ms like the loop does */
    inline void runMachine(FakeMachine& m, rd::RoundUi& ui, const float seconds) {
        for (float t = 0.0f; t < seconds; t += 0.1f) {
            m.step(0.1f);
            gSimulatedMs += 100;
            ui.update(m.model(), gSimulatedMs);
        }
    }

    /** Lets the machine run until the ready label turns green, then on for extraMs */
    inline void runUntilReady(FakeMachine& m, rd::RoundUi& ui, const uint32_t extraMs) {
        for (int i = 0; i < 3000 && !(ui.screen() == rd::Screen::Ready && ui.ready()); ++i) {
            runMachine(m, ui, 0.1f);
        }

        runMachine(m, ui, static_cast<float>(extraMs) / 1000.0f);
    }

    /** One shot by time (25 s) from switch on to switch off */
    inline void pullShot(FakeMachine& m, rd::RoundUi& ui, const float targetSeconds) {
        m.targetBrewTime = targetSeconds;
        m.toggleBrewSwitch();
        runMachine(m, ui, targetSeconds + 0.5f);
        m.toggleBrewSwitch();
        runMachine(m, ui, 20.0f);
    }

    inline const rd::Message msgVersion{"VERSION", "CleverCoffee", "4.0.3 + Rund-Display"};
    inline const rd::Message msgWifi{"WLAN", "Verbinde mit", "Orione-WLAN"};
    inline const rd::Message msgIp{"IP-ADRESSE", "silvia.local", "192.168.178.42"};
    inline const rd::Message msgPortal{"WLAN-EINRICHTUNG", "Hotspot: silvia", "192.168.4.1"};

    /** Shows a firmware message string ("Title\nline\n...") the way roundDisplayMessage() does */
    inline void showFirmwareMessage(rd::RoundUi& ui, const char* text) {
        static rd::MessageText buffer;
        rd::splitMessage(text, buffer);
        ui.showMessage(buffer.message());
    }

    inline std::vector<Scenario> scenarios() {
        return {
            {"intro-1", "Intro 0,45 s",
             [](FakeMachine& m, rd::RoundUi& ui) {
                 ui.showMessage(msgVersion);
                 ui.play(rd::Animation::Intro, 0);
             },
             450},
            {"intro-2", "Intro 1,0 s",
             [](FakeMachine& m, rd::RoundUi& ui) {
                 ui.showMessage(msgVersion);
                 ui.play(rd::Animation::Intro, 0);
             },
             1000},
            {"reveal", "Blende auf 0,35 s",
             [](FakeMachine& m, rd::RoundUi& ui) {
                 m.reset(61.4f);
                 m.setHeater(100);
                 ui.update(m.model(), 0);
                 ui.play(rd::Animation::Reveal, 0);
             },
             350},
            {"close", "Blende zu 0,45 s",
             [](FakeMachine& m, rd::RoundUi& ui) {
                 m.settle();
                 m.setLastShot(25.3f);
                 ui.update(m.model(), 0);
                 ui.play(rd::Animation::Close, 0);
             },
             450},
            {"boot", "Start", [](FakeMachine& m, rd::RoundUi& ui) { ui.showMessage(msgVersion); }},
            {"wifi", "WLAN verbinden", [](FakeMachine& m, rd::RoundUi& ui) { ui.showMessage(msgWifi); }},
            {"ip", "IP-Adresse", [](FakeMachine& m, rd::RoundUi& ui) { ui.showMessage(msgIp); }},
            {"portal", "WLAN-Einrichtung", [](FakeMachine& m, rd::RoundUi& ui) { ui.showMessage(msgPortal); }},
            {"msg-offline", "Offline-Hinweis (4 Zeilen)", [](FakeMachine&, rd::RoundUi& ui) { showFirmwareMessage(ui, "Kein WLAN\nOffline AP starten\nsilvia\n192.168.4.1"); }},
            {"msg-taring", "Waage tarieren", [](FakeMachine&, rd::RoundUi& ui) { showFirmwareMessage(ui, "Taring scale,\nremove any load!\n....\ndone"); }},
            {"msg-calibrate-en", "Kalibrierung (lang, EN)", [](FakeMachine&, rd::RoundUi& ui) { showFirmwareMessage(ui, "Calibration in progress. Place known weight on scale in next 10 seconds 267.00g\n"); }},
            {"msg-calibrate-de", "Kalibrierung (lang, DE)", [](FakeMachine&, rd::RoundUi& ui) { showFirmwareMessage(ui, "Kalibrierung läuft. Bitte in den nächsten 10 Sekunden ein bekanntes Gewicht auflegen 267.00g\n"); }},
            {"msg-calibrate-es", "Kalibrierung (lang, ES)", [](FakeMachine&, rd::RoundUi& ui) { showFirmwareMessage(ui, "Calibrando. Coloque un peso conocido en la balanza en los próximos 10 segundos 267.00g\n"); }},
            {"msg-reboot", "Neustart", [](FakeMachine&, rd::RoundUi& ui) { showFirmwareMessage(ui, "REBOOTING\nPlease wait..."); }},
            {"trend-up", "Aufheizen nach 30 s (Tendenz steigt)", [](FakeMachine& m, rd::RoundUi& ui) { runMachine(m, ui, 30.0f); }},
            {"trend-down", "Abkühlen nach Dampf (Tendenz fällt)",
             [](FakeMachine& m, rd::RoundUi& ui) {
                 m.toggleSteam();
                 m.settle();
                 runMachine(m, ui, 2.0f);
                 m.toggleSteam();
                 runMachine(m, ui, 8.0f);
             }},
            {"ready-moment", "Bereit-Moment (+0,6 s)", [](FakeMachine& m, rd::RoundUi& ui) { runUntilReady(m, ui, 600); }},
            {"brew-shimmer", "Bezug mit Lichtreflex (12,5 s)",
             [](FakeMachine& m, rd::RoundUi& ui) {
                 m.settle();
                 m.toggleBrewSwitch();
                 runMachine(m, ui, 12.5f);
             }},
            {"done-average", "Bezug fertig mit Ø-Temperatur",
             [](FakeMachine& m, rd::RoundUi& ui) {
                 m.settle();
                 m.toggleBrewSwitch();
                 runMachine(m, ui, 26.0f);
             }},
            {"shot-history", "Nach fünf Bezügen (Verlauf)",
             [](FakeMachine& m, rd::RoundUi& ui) {
                 m.settle();

                 for (const float t : {25.0f, 27.5f, 25.0f, 24.5f, 25.0f}) {
                     pullShot(m, ui, t);
                 }

                 runMachine(m, ui, 60.0f);
             }},
            {"scale-ok", "Waage verbunden",
             [](FakeMachine& m, rd::RoundUi&) {
                 m.scale = true;
                 m.settle();
                 m.setHeater(18);
                 m.setLastShot(25.3f);
             }},
            {"scale-lost", "Waage nicht verbunden",
             [](FakeMachine& m, rd::RoundUi&) {
                 m.scale = true;
                 m.scaleConnected = false;
                 m.settle();
                 m.setHeater(18);
                 m.setLastShot(25.3f);
             }},
            {"scale-fault", "Waage gestört",
             [](FakeMachine& m, rd::RoundUi&) {
                 m.scale = true;
                 m.scaleBroken = true;
                 m.settle();
                 m.setHeater(18);
                 m.setLastShot(25.3f);
             }},
            {"scale-heating", "Aufheizen mit Waage",
             [](FakeMachine& m, rd::RoundUi&) {
                 m.scale = true;
                 m.reset(61.4f);
                 m.scale = true;
                 m.setHeater(100);
             }},
            {"heating-flush", "Aufheizen, spült danach von selbst",
             [](FakeMachine& m, rd::RoundUi&) {
                 m.reset(61.4f);
                 m.setHeater(100);
                 m.warmupFlushPending = true;
             }},
            {"ready-flush", "Bereit, spült gleich von selbst",
             [](FakeMachine& m, rd::RoundUi&) {
                 m.settle();
                 m.nudgeTemperature(0.1f);
                 m.setHeater(18);
                 m.warmupFlushPending = true;
             }},
            {"heating", "Aufheizen",
             [](FakeMachine& m, rd::RoundUi&) {
                 m.reset(61.4f);
                 m.setHeater(100);
             }},
            {"ready", "Bereit (Letzter Bezug)",
             [](FakeMachine& m, rd::RoundUi&) {
                 m.settle();
                 m.nudgeTemperature(0.1f);
                 m.setHeater(18);
                 m.setLastShot(25.3f);
             }},
            {"below", "Knapp unter Soll",
             [](FakeMachine& m, rd::RoundUi&) {
                 m.settle();
                 m.nudgeTemperature(-1.8f);
                 m.setHeater(64);
             }},
            {"above", "Über Soll",
             [](FakeMachine& m, rd::RoundUi&) {
                 m.settle();
                 m.nudgeTemperature(2.4f);
                 m.setHeater(0);
             }},
            {"brew", "Bezug (nach Zeit)",
             [](FakeMachine& m, rd::RoundUi&) {
                 m.settle();
                 m.nudgeTemperature(-1.1f);
                 m.setHeater(100);
                 m.setBrewing(12.4f, 0);
             }},
            {"brew-scale", "Bezug mit Waage",
             [](FakeMachine& m, rd::RoundUi&) {
                 m.scale = true;
                 m.settle();
                 m.nudgeTemperature(-0.7f);
                 m.setHeater(100);
                 m.setBrewing(18.2f, 21.6f);
             }},
            {"scale-missing", "Nach Gewicht, Waage nicht verbunden: Stopp nach Zeit",
             [](FakeMachine& m, rd::RoundUi&) {
                 m.scale = true;
                 m.scaleConnected = false;
                 m.settle();
                 m.nudgeTemperature(-0.7f);
                 m.setHeater(100);
                 m.setBrewing(12.4f, 0);
             }},
            {"scale-8", "Waage, Bezug 8 s", [](FakeMachine& m, rd::RoundUi& ui) { runScaleShot(m, ui, 8.0f); }},
            {"scale-18", "Waage, Bezug 18 s", [](FakeMachine& m, rd::RoundUi& ui) { runScaleShot(m, ui, 18.0f); }},
            {"scale-done", "Waage, Bezug fertig", [](FakeMachine& m, rd::RoundUi& ui) { runScaleShot(m, ui, 27.8f); }},
            {"scale-done-remind", "Waage, fertig, Schalter seit 1 min an",
             [](FakeMachine& m, rd::RoundUi& ui) {
                 runScaleShot(m, ui, 27.8f);

                 for (float t = 27.8f; t < 95.0f; t += 0.1f) { // switch left on
                     m.step(0.1f);
                     ui.update(m.model(), static_cast<uint32_t>(t * 1000.0f));
                 }
             }},
            {"scale-time-remind", "Waage, nach Zeit fertig, Schalter seit 1 min an",
             [](FakeMachine& m, rd::RoundUi& ui) {
                 m.scale = true;
                 m.targetBrewWeight = 0; // stop by time, the scale only weighs
                 m.settle();
                 m.toggleBrewSwitch();

                 for (float t = 0.0f; t < 95.0f; t += 0.1f) { // stops at 25 s, switch left on
                     m.step(0.1f);
                     ui.update(m.model(), static_cast<uint32_t>(t * 1000.0f));
                 }
             }},
            {"scale-done-over", "Waage, fertig, 2,5 g über dem Ziel",
             [](FakeMachine& m, rd::RoundUi&) {
                 m.scale = true;
                 m.settle();
                 m.setBrewDone(28.4f, 38.5f);
             }},
            {"scale-done-under", "Waage, fertig, knapp unter dem Ziel",
             [](FakeMachine& m, rd::RoundUi&) {
                 m.scale = true;
                 m.settle();
                 m.setBrewDone(26.9f, 35.2f);
             }},
            {"done", "Bezug fertig",
             [](FakeMachine& m, rd::RoundUi&) {
                 m.settle();
                 m.nudgeTemperature(-0.9f);
                 m.setBrewDone(25.0f, 0);
             }},
            {"flush", "Spülen",
             [](FakeMachine& m, rd::RoundUi&) {
                 m.settle();
                 m.setFlush(6.2f);
             }},
            {"hotwater", "Heißwasser",
             [](FakeMachine& m, rd::RoundUi&) {
                 m.settle();
                 m.setHotWater(14.0f);
             }},
            {"steam", "Dampf",
             [](FakeMachine& m, rd::RoundUi&) {
                 m.settle();
                 m.toggleSteam();
                 m.nudgeTemperature(18.0f);
                 m.setHeater(100);
             }},
            {"backflush", "Rückspülen (Zyklus 3/5)",
             [](FakeMachine& m, rd::RoundUi&) {
                 m.settle();
                 m.setBackflush(rd::BackflushPhase::Flushing, 3);
             }},
            {"backflush-start", "Rückspülen (Start)",
             [](FakeMachine& m, rd::RoundUi&) {
                 m.settle();
                 m.setBackflush(rd::BackflushPhase::Idle, 0);
             }},
            {"water", "Wassertank leer",
             [](FakeMachine& m, rd::RoundUi&) {
                 m.settle();
                 m.toggleWaterEmpty();
             }},
            {"sensor", "Sensorfehler", [](FakeMachine& m, rd::RoundUi&) { m.toggleSensorError(); }},
            {"overtemp", "Übertemperatur", [](FakeMachine& m, rd::RoundUi&) { m.triggerOvertemperature(); }},
            {"standby", "Standby",
             [](FakeMachine& m, rd::RoundUi&) {
                 m.reset(71.0f);
                 m.toggleStandby();
             }},
            {"pid-off", "PID aus",
             [](FakeMachine& m, rd::RoundUi&) {
                 m.reset(48.3f);
                 m.togglePid();
             }},
            {"no-wifi", "WLAN weg",
             [](FakeMachine& m, rd::RoundUi&) {
                 m.wifiConnected = false;
                 m.settle();
                 m.setLastShot(24.8f);
             }},
            {"english", "Englisch (--lang en)",
             [](FakeMachine& m, rd::RoundUi&) {
                 m.language = rd::Language::English;
                 m.settle();
                 m.nudgeTemperature(-0.2f);
                 m.setLastShot(25.3f);
             }},
        };
    }

    /** Renders one scenario into the panel (fresh machine and UI each time) */
    inline void renderScenario(const Scenario& sc, Panel& panel, const rd::Language lang, const char* brand = kDefaultBrand) {
        FakeMachine machine;
        machine.language = lang;
        machine.reset();
        rd::RoundUi ui;
        ui.setBrand(brand);
        gSimulatedMs = 0;
        sc.setup(machine, ui);
        const uint32_t at = gSimulatedMs > 0 ? gSimulatedMs + sc.atMs : sc.atMs; // simulated scenarios: atMs is an offset
        ui.update(machine.model(), at);
        panel.render(ui, at);
    }

    inline const Scenario* findScenario(const std::vector<Scenario>& all, const char* name) {

        for (const auto& s : all) {
            if (std::strcmp(s.name, name) == 0) {
                return &s;
            }
        }

        return nullptr;
    }

} // namespace sim
