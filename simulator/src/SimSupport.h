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
            {"scale-8", "Waage, Bezug 8 s", [](FakeMachine& m, rd::RoundUi& ui) { runScaleShot(m, ui, 8.0f); }},
            {"scale-18", "Waage, Bezug 18 s", [](FakeMachine& m, rd::RoundUi& ui) { runScaleShot(m, ui, 18.0f); }},
            {"scale-done", "Waage, Bezug fertig", [](FakeMachine& m, rd::RoundUi& ui) { runScaleShot(m, ui, 27.8f); }},
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
        sc.setup(machine, ui);
        ui.update(machine.model(), sc.atMs);
        panel.render(ui, sc.atMs);
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
