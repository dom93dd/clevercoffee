/**
 * @file roundDisplay.h
 *
 * @brief Connects the round display UI (lib/RoundDisplay) to the firmware.
 *        Replaces displayTemplateManager.h in builds with -D ROUND_DISPLAY.
 *
 * Collects the machine state from the globals into an rd::Model and draws the UI
 * band by band (2 x 240x40 pixel, 19 KB each) with DMA, so the ESP32 needs no
 * frame buffer. Also provides the display functions the rest of the firmware calls
 * (displayLogo, displayWrappedMessage, displayScaleFailed, shouldDisplayBrewTimer).
 */

#pragma once

#include <RoundDisplayUi.h>

#include "languages.h"

// Name in the intro and above boot messages (capitals, see the label font in lib/RoundDisplay)
#ifndef ROUND_DISPLAY_BRAND
#define ROUND_DISPLAY_BRAND "DOMS COFFEE"
#endif

constexpr int kRoundBandHeight = 40;
constexpr unsigned long kRoundModelInterval = 50; // ms between two looks at the machine state

inline RoundTft* roundTft = nullptr;
inline lgfx::LGFX_Sprite* roundBands = nullptr;
inline rd::RoundUi roundUi;
inline char roundMessageText[4][48];
inline bool roundPanelAsleep = false;
inline float roundLastShotSeconds = 0;

/**
 * @brief determines if brew timer should be visible; postBrewTimerDuration defines how long the timer after the brew is shown
 *        (same logic as in displayCommon.h)
 */
inline bool shouldDisplayBrewTimer() {
    enum BrewTimerState {
        kBrewTimerIdle = 10,
        kBrewTimerRunning = 20,
        kBrewTimerPostBrew = 30
    };

    static BrewTimerState currBrewTimerState = kBrewTimerIdle;
    static uint32_t brewEndTime = 0;

    switch (currBrewTimerState) {
        case kBrewTimerIdle:
            if (checkBrewActive()) {
                currBrewTimerState = kBrewTimerRunning;
            }
            break;

        case kBrewTimerRunning:
            if (!checkBrewActive()) {
                currBrewTimerState = kBrewTimerPostBrew;
                brewEndTime = millis();
                roundLastShotSeconds = static_cast<float>(currBrewTime / 1000);
            }
            break;

        case kBrewTimerPostBrew:
            if (millis() - brewEndTime > static_cast<uint32_t>(postBrewTimerDuration * 1000)) {
                currBrewTimerState = kBrewTimerIdle;
            }
            break;
    }

    return currBrewTimerState != kBrewTimerIdle;
}

inline rd::Mode roundDisplayMode() {
    switch (machineState) {
        case kPidNormal:
            return rd::Mode::Normal;
        case kBrew:
            return rd::Mode::Brew;
        case kManualFlush:
            return rd::Mode::ManualFlush;
        case kSteam:
            return rd::Mode::Steam;
        case kHotWater:
            return rd::Mode::HotWater;
        case kBackflush:
            return rd::Mode::Backflush;
        case kPidDisabled:
            return rd::Mode::PidDisabled;
        case kWaterTankEmpty:
            return rd::Mode::WaterTankEmpty;
        case kStandby:
            return rd::Mode::Standby;
        case kEmergencyStop:
            return rd::Mode::EmergencyStop;
        case kSensorError:
            return rd::Mode::SensorError;
        default:
            return rd::Mode::Init;
    }
}

inline rd::Model roundDisplayModel() {
    rd::Model m;

    m.mode = roundDisplayMode();
    m.language = config.get<int>("display.language") == 0 ? rd::Language::German : rd::Language::English;

    m.temperature = static_cast<float>(temperature);
    m.setpoint = static_cast<float>(setpoint);
    m.heaterPercent = static_cast<float>(pidOutput / 10);
    m.readyBand = config.get<float>("display.blinking.delta");
    m.emergencyResetTemp = static_cast<float>(brewSetpoint + 5);

    m.brewTimerVisible = config.get<bool>("hardware.switches.brew.enabled") && shouldDisplayBrewTimer();

    switch (currBrewState) {
        case kPreinfusion:
            m.brewPhase = rd::BrewPhase::Preinfusion;
            break;
        case kPreinfusionPause:
            m.brewPhase = rd::BrewPhase::PreinfusionPause;
            break;
        case kBrewRunning:
            m.brewPhase = rd::BrewPhase::Running;
            break;
        case kBrewFinished:
            m.brewPhase = rd::BrewPhase::Finished;
            break;
        default:
            m.brewPhase = rd::BrewPhase::Idle;
            break;
    }

    m.brewTime = static_cast<float>(currBrewTime / 1000);
    m.brewTargetTime = static_cast<float>(totalTargetBrewTime / 1000);
    m.lastBrewTime = roundLastShotSeconds;
    m.flushTime = static_cast<float>(currBrewTime / 1000);
    m.hotWaterTime = static_cast<float>(currPumpOnTime / 1000);

    m.scaleEnabled = scale != nullptr && config.get<bool>("hardware.sensors.scale.enabled");

    if (m.scaleEnabled) {
        m.scaleFault = scaleFailure;
        m.bleScale = config.get<int>("hardware.sensors.scale.type") == 2;
        m.bleScaleConnected = scale->isConnected();
        m.brewWeight = currBrewWeight;

        if (config.get<bool>("brew.by_weight.enabled") && config.get<int>("brew.mode") != 0) {
            m.brewTargetWeight = config.get<float>("brew.by_weight.target_weight");
        }
    }

    switch (currBackflushState) {
        case kBackflushFilling:
            m.backflushPhase = rd::BackflushPhase::Filling;
            break;
        case kBackflushFlushing:
            m.backflushPhase = rd::BackflushPhase::Flushing;
            break;
        case kBackflushEnding:
            m.backflushPhase = rd::BackflushPhase::Ending;
            break;
        case kBackflushFinished:
            m.backflushPhase = rd::BackflushPhase::Finished;
            break;
        default:
            m.backflushPhase = rd::BackflushPhase::Idle;
            break;
    }

    m.backflushCycle = static_cast<uint8_t>(currBackflushCycles);
    m.backflushCycles = static_cast<uint8_t>(backflushCycles);

    m.offlineMode = offlineMode;
    m.wifiConnected = WiFi.status() == WL_CONNECTED;
    m.wifiBars = static_cast<uint8_t>(getSignalStrength());
    m.mqttEnabled = mqtt_enabled;
    m.mqttConnected = mqtt_enabled && mqtt.connected();

    return m;
}

inline void roundDisplayRender() {
    const uint32_t now = millis();

    roundTft->startWrite();
    roundUi.render(roundBands, 2, now, [](lgfx::LGFX_Sprite& band, const int top) {
        // Waits for the previous band, then sends this one while the CPU draws the next
        roundTft->pushImageDMA(0, top, band.width(), band.height(), static_cast<lgfx::swap565_t*>(band.getBuffer()));
    });
    roundTft->endWrite();
}

/**
 * @brief Shows a message right away (used during setup, before the loop runs)
 */
inline void roundDisplayMessage(const String& text) {
    if (roundTft == nullptr) {
        return;
    }

    if (roundPanelAsleep) {
        roundTft->wakeup();
        roundPanelAsleep = false;
    }

    // Split into title and up to three lines; the title is shown in capitals
    const char* lines[4] = {nullptr, nullptr, nullptr, nullptr};
    int start = 0;

    for (int i = 0; i < 4 && start <= static_cast<int>(text.length()); ++i) {
        int end = text.indexOf('\n', start);

        if (end < 0) {
            end = static_cast<int>(text.length());
        }

        String part = text.substring(start, end);
        part.trim();

        if (i == 0) {
            part.toUpperCase(); // ASCII only; German umlauts follow below

            for (unsigned int k = 0; k + 1 < part.length(); ++k) {
                const auto c = static_cast<uint8_t>(part[k + 1]);

                if (static_cast<uint8_t>(part[k]) == 0xC3 && c >= 0xA0 && c <= 0xBE && c != 0xB7) {
                    part.setCharAt(k + 1, static_cast<char>(c - 0x20)); // ä -> Ä, ö -> Ö, ü -> Ü, é -> É ...
                }
            }
        }

        snprintf(roundMessageText[i], sizeof(roundMessageText[i]), "%s", part.c_str());
        lines[i] = roundMessageText[i];
        start = end + 1;
    }

    roundUi.showMessage(rd::Message{lines[0], lines[1], lines[2], lines[3]});
    roundUi.update(roundDisplayModel(), millis());
    roundDisplayRender();
}

inline void displayLogo(const String& displaymessagetext, boolean wrap = false) {
    roundDisplayMessage(displaymessagetext);
}

inline void displayWrappedMessage(const String& message, int x, int startY, int spacing, boolean clearSend, boolean wrapWord) {
    roundDisplayMessage(message);
}

inline void displayScaleFailed() {
    roundDisplayMessage("Scale\nnot working...");
}

/**
 * @brief Plays the power-on intro (1.7 s). Blocks, like the other boot messages; the heater
 *        stays off during setup() anyway.
 */
inline void roundDisplayPlayIntro() {
    const rd::Model idle; // nothing to show yet
    uint32_t now = millis();
    roundUi.update(idle, now);
    roundUi.play(rd::Animation::Intro, now);

    while (roundUi.animating(now)) {
        roundUi.update(idle, now);

        if (roundUi.needsRedraw(now)) {
            roundDisplayRender();
        }

        delay(1);
        now = millis();
    }
}

inline bool roundDisplayInit() {
    roundUi.setBrand(ROUND_DISPLAY_BRAND);

    if (!rd::RoundUi::begin()) {
        LOG(ERROR, "Round display: fonts could not be loaded");
        return false;
    }

    roundTft = new RoundTft();

    if (!roundTft->init()) {
        LOG(ERROR, "Round display: panel init failed");
        return false;
    }

    roundTft->fillScreen(TFT_BLACK);
    roundBands = new lgfx::LGFX_Sprite[2];

    for (int i = 0; i < 2; ++i) {
        roundBands[i].setColorDepth(16);

        if (roundBands[i].createSprite(rd::RoundUi::kWidth, kRoundBandHeight) == nullptr) {
            LOG(ERROR, "Round display: not enough RAM for the band buffers");
            return false;
        }
    }

    u8g2 = new RoundDisplayPower();
    LOGF(INFO, "Round display ready, free heap %u bytes", ESP.getFreeHeap());
    roundDisplayPlayIntro();
    return true;
}

/**
 * @brief Called every loop: takes the machine state and redraws when something visible changed
 */
inline void roundDisplayLoop() {
    static bool messageCleared = false;
    static bool closing = false;
    static unsigned long lastModel = 0;
    static unsigned long offlineNoticeStart = 0;

    if (roundTft == nullptr || u8g2 == nullptr) {
        return;
    }

    const unsigned long now = millis();

    // Look at the machine every 50 ms; during transitions as often as frames are due
    if (!roundUi.animating(now) && !closing && now - lastModel < kRoundModelInterval) {
        return;
    }

    lastModel = now;

    // Boot and WiFi messages stay until the loop runs
    if (!messageCleared) {
        roundUi.clearMessage();
        messageCleared = true;
    }

    // WiFi lost for good at runtime: show the offline access point for a few seconds (displayOffline as in displayCommon.h)
    if (displayOffline > 0 && displayOffline < 50) {
        if (offlineNoticeStart == 0) {
            offlineNoticeStart = now;
            roundDisplayMessage(String(langstring_nowifi[0]) + String(langstring_nowifi[1]) + "\n" + String(langstring_offlineAP) + "\n" + hostname + "\n" + WiFi.softAPIP().toString());
        }

        if (now - offlineNoticeStart < 5000) {
            return;
        }

        displayOffline = 50;
        roundUi.clearMessage();
    }

    // Display off (standby) and on again: close the iris before the panel sleeps, open it after waking up
    const bool sleepWanted = u8g2->sleepRequested();

    if (sleepWanted && !roundPanelAsleep) {
        if (!closing) {
            roundUi.play(rd::Animation::Close, now);
            closing = true;
        }
        else if (!roundUi.animating(now)) {
            roundTft->sleep();
            roundPanelAsleep = true;
            closing = false;
            return;
        }
    }
    else if (!sleepWanted && roundPanelAsleep) {
        roundTft->wakeup();
        roundPanelAsleep = false;
        roundUi.invalidate();
        roundUi.play(rd::Animation::Reveal, now);
    }
    else if (!sleepWanted) {
        closing = false; // woken up before the iris was closed
    }

    if (roundPanelAsleep) {
        return;
    }

    roundUi.update(roundDisplayModel(), now);

    // Like the OLED: skip loops where other slow work runs, but never wait longer than 500 ms
    const bool busy = websiteUpdateRunning || mqttUpdateRunning || hassioUpdateRunning || temperatureUpdateRunning;

    if (roundUi.needsRedraw(now) && (!busy || now - lastDisplayUpdate > 500)) {
        displayUpdateRunning = true;
        roundDisplayRender();
        lastDisplayUpdate = now;
    }
}
