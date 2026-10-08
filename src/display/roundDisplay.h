/**
 * @file roundDisplay.h
 *
 * @brief Connects the round display UI (lib/RoundDisplay) to the firmware.
 *        Replaces displayTemplateManager.h in builds with -D ROUND_DISPLAY.
 *
 * Collects the machine state from the globals into an rd::Model and draws the UI
 * band by band (2 x 240x40 pixel, 19 KB each) with DMA, so the ESP32 needs no
 * frame buffer. In loop() one band per iteration, so loop() never waits for a whole frame;
 * bands the panel already shows are not sent again. Also provides the display functions the rest of the firmware calls
 * (displayLogo, displayWrappedMessage, displayScaleFailed, shouldDisplayBrewTimer).
 * Everything that works without hardware lives in RoundDisplayControl and is tested
 * in simulator/test (pio test -e test).
 */

#pragma once

#include <RoundDisplayControl.h>
#include <RoundDisplayUi.h>

#include "languages.h"

// Name in the intro and above boot messages (capitals, see the label font in lib/RoundDisplay)
#ifndef ROUND_DISPLAY_BRAND
#define ROUND_DISPLAY_BRAND "DOMS COFFEE"
#endif

// The UI maps the firmware states by number; fail the build if they ever change
static_assert(kInit == rd::firmware::kInit && kPidNormal == rd::firmware::kPidNormal && kBrew == rd::firmware::kBrew && kManualFlush == rd::firmware::kManualFlush && kSteam == rd::firmware::kSteam &&
                  kHotWater == rd::firmware::kHotWater && kBackflush == rd::firmware::kBackflush && kPidDisabled == rd::firmware::kPidDisabled && kWaterTankEmpty == rd::firmware::kWaterTankEmpty &&
                  kStandby == rd::firmware::kStandby && kEmergencyStop == rd::firmware::kEmergencyStop && kSensorError == rd::firmware::kSensorError,
              "MachineState numbers changed, update rd::firmware in RoundDisplayControl.h");
static_assert(kBrewIdle == rd::firmware::kBrewIdle && kPreinfusion == rd::firmware::kPreinfusion && kPreinfusionPause == rd::firmware::kPreinfusionPause && kBrewRunning == rd::firmware::kBrewRunning &&
                  kBrewFinished == rd::firmware::kBrewFinished,
              "BrewState numbers changed, update rd::firmware in RoundDisplayControl.h");
static_assert(kBackflushIdle == rd::firmware::kBackflushIdle && kBackflushFilling == rd::firmware::kBackflushFilling && kBackflushFlushing == rd::firmware::kBackflushFlushing &&
                  kBackflushEnding == rd::firmware::kBackflushEnding && kBackflushFinished == rd::firmware::kBackflushFinished,
              "BackflushState numbers changed, update rd::firmware in RoundDisplayControl.h");

constexpr int kRoundBandHeight = 40;
constexpr unsigned long kRoundModelInterval = 50; // ms between two looks at the machine state

inline RoundTft* roundTft = nullptr;
inline lgfx::LGFX_Sprite* roundBands = nullptr;
inline rd::RoundUi roundUi;
inline rd::BrewTimer roundBrewTimer;
inline rd::PowerSequencer roundPower;
inline rd::MessageText roundMessageText;
inline rd::BandFilter roundBandFilter; // bands the panel already shows are not sent again

/**
 * @brief determines if brew timer should be visible; postBrewTimerDuration defines how long the timer after the brew is shown
 */
inline bool shouldDisplayBrewTimer() {
    // a shot that stopped by itself stays on the display while the brew switch is still on (Dominik, 07.10.2026)
    const bool visible = roundBrewTimer.update(checkBrewActive(), static_cast<float>(currBrewTime / 1000), millis(), static_cast<float>(postBrewTimerDuration),
                                               brewSwitchHeldAfterBrew());
    static bool wasHeld = false;

    if (roundBrewTimer.held() != wasHeld) {
        wasHeld = !wasHeld;
        LOG(INFO, wasHeld ? "Round display: the shot stays until the brew switch is off" : "Round display: brew switch off, the shot stays for the post-brew time");
    }

    return visible;
}

inline rd::Model roundDisplayModel() {
    rd::Model m;

    m.mode = rd::modeFromMachineState(machineState);
    m.language = config.get<int>("display.language") == 0 ? rd::Language::German : rd::Language::English;

    m.temperature = static_cast<float>(temperature);
    m.setpoint = static_cast<float>(setpoint);
    m.heaterPercent = static_cast<float>(pidOutput / 10);
#ifdef CC_ORIONE
    // Steam from the original switch: seen from the temperature (orioneMachine.h). Only over the normal screen,
    // never over a shot, an alarm or the like; the gauge's top is the steam setting (the thermostat holds ~120-130 °C).
    if (m.mode == rd::Mode::Normal) {
        if (orione_machine::steam.phase() == orione::SteamWatch::kSteam) {
            m.mode = rd::Mode::Steam;
            m.steamByThermostat = true;
            m.setpoint = config.get<float>("steam.setpoint");
        }
        else {
            m.steamCooling = orione_machine::steam.phase() == orione::SteamWatch::kCooling;
        }
    }
#endif
    m.readyBand = config.get<float>("display.blinking.delta");
    m.emergencyResetTemp = static_cast<float>(brewSetpoint + 5);

    m.brewTimerVisible = config.get<bool>("hardware.switches.brew.enabled") && shouldDisplayBrewTimer();
    m.brewPhase = rd::brewPhaseFromState(currBrewState);
    m.brewTime = static_cast<float>(currBrewTime / 1000);
#ifdef CC_ORIONE
    // the target only when the time ends the shot, as on the web page: by time, or by weight without the scale
    // (for the shot on the screen: as it ran; otherwise: is the scale there now). By hand, or by weight with
    // the scale, the ring is a stopwatch.
    const bool automatic = config.get<int>("brew.mode") != 0;
    const bool weightWithoutScale = automatic && config.get<bool>("brew.by_weight.enabled") && (m.brewTimerVisible ? brewWeightFallback : !brewScaleReady());
    const bool stopByTime = automatic && (config.get<bool>("brew.by_time.enabled") || weightWithoutScale);
    m.brewTargetTime = stopByTime ? static_cast<float>(totalTargetBrewTime / 1000) : 0.0f;
    m.warmupFlushPending = warmup_flush::livePhase() == orione::WarmupFlush::kWaiting;
    m.flushReminder = shot_history::flushPending;
#else
    m.brewTargetTime = static_cast<float>(totalTargetBrewTime / 1000);
#endif
    m.lastBrewTime = roundBrewTimer.lastShotSeconds();
    m.brewSwitchReminder = roundBrewTimer.remind(millis());
    m.flushTime = static_cast<float>(currBrewTime / 1000);
    m.hotWaterTime = static_cast<float>(currPumpOnTime / 1000);

    m.scaleEnabled = scale != nullptr && config.get<bool>("hardware.sensors.scale.enabled");

    if (m.scaleEnabled) {
#ifdef CC_ORIONE
        // a Bluetooth scale that is off (or asleep) is not connected, not broken: no red symbol, no "Waage gestört"
        // in every shot after 30 s without it (scaleFailure); the web page says "nicht verbunden" as well
        m.scaleFault = false;
#else
        m.scaleFault = scaleFailure;
#endif
#ifdef CC_ORIONE
        m.bleScale = true; // the Orione build has the Bluetooth scale only
#else
        m.bleScale = config.get<int>("hardware.sensors.scale.type") == 2;
#endif
        m.bleScaleConnected = scale->isConnected();
#ifdef CC_ORIONE
        // After the stop: what is in the cup with the drops, as the web page shows it; the highest reading
        // while they are counted (shotHistory.h), so lifting the cup afterwards does not take it back
        static float cup = 0.0f;

        if (checkBrewActive()) {
            cup = currBrewWeight;
        }
        else if (shot_history::shotLog.settling()) {
            cup = std::max(cup, static_cast<float>(currReadingWeight - preBrewWeight));
        }

        m.brewWeight = cup;
#else
        m.brewWeight = currBrewWeight;
#endif

        if (config.get<bool>("brew.by_weight.enabled") && config.get<int>("brew.mode") != 0) {
            m.brewTargetWeight = config.get<float>("brew.by_weight.target_weight");
        }
#ifdef CC_ORIONE
        if (weightWithoutScale) {
            m.brewTargetWeight = 0.0f; // the time ring, also if the scale comes back during the shot
        }
#endif
    }

    m.backflushPhase = rd::backflushPhaseFromState(currBackflushState);
    m.backflushCycle = static_cast<uint8_t>(currBackflushCycles);
    m.backflushCycles = static_cast<uint8_t>(backflushCycles);

    m.offlineMode = offlineMode;
    m.wifiConnected = WiFi.status() == WL_CONNECTED;
    m.wifiBars = static_cast<uint8_t>(getSignalStrength());
    m.mqttEnabled = mqtt_enabled;
    m.mqttConnected = mqtt_enabled && mqtt.connected();

    return m;
}

inline bool roundFrameSending = false; // SPI transaction stays open while a frame is drawn band by band

inline void roundPushBand(lgfx::LGFX_Sprite& band, const int top) {
    if (!roundBandFilter.changed(top / band.height(), static_cast<const uint16_t*>(band.getBuffer()), band.width() * band.height())) {
        // Unchanged: nothing to send, but the other buffer may still be on its way and is drawn into next
        roundTft->waitDMA();
        return;
    }

    // Waits for the previous band, then sends this one while the CPU draws the next
    roundTft->pushImageDMA(0, top, band.width(), band.height(), static_cast<lgfx::swap565_t*>(band.getBuffer()));
}

/**
 * @brief Draws a whole frame at once (boot messages and intro, while setup() may block anyway)
 */
inline void roundDisplayRender() {
    if (roundFrameSending) { // a frame drawn band by band is dropped, this one replaces it
        roundTft->endWrite();
        roundFrameSending = false;
    }

    roundTft->startWrite();
    roundUi.render(roundBands, 2, millis(), roundPushBand);
    roundTft->endWrite();
}

/**
 * @brief Draws the next band of the current frame (opens a frame if none is open). One band per
 *        loop() keeps loop() short (a few ms) while a frame takes a few loop iterations; the DMA
 *        transfer of a band runs on while loop() does its other work.
 */
inline void roundDisplayStep(const uint32_t now) {
    ROUND_TIME(Band);

    if (!roundFrameSending) {
        roundTft->startWrite();
        roundFrameSending = true;
        ROUND_TIMING_DO(round_timing::frameBegin());
    }

    if (roundUi.renderBand(roundBands, 2, roundPushBand, now)) {
        roundTft->endWrite(); // waits for the last transfer
        roundFrameSending = false;
        ROUND_TIMING_DO(round_timing::frameEnd());
    }
}

/**
 * @brief Shows a message right away (used during setup, before the loop runs)
 */
inline void roundDisplayMessage(const String& text) {
    if (roundTft == nullptr) {
        return;
    }

    if (roundPower.asleep()) {
        roundTft->wakeup();
        roundBandFilter.invalidate();
    }

    rd::splitMessage(text.c_str(), roundMessageText);
    roundUi.showMessage(roundMessageText.message());
    roundUi.update(roundDisplayModel(), millis());
    roundDisplayRender();
}

/**
 * @brief WiFi setup screen while the setup portal is open: "WLAN EINRICHTEN", a QR code that joins
 *        the setup WiFi from the phone camera, below it on the first setup name and password of
 *        the setup WiFi (for typing by hand), with a saved WiFi out of reach (router off?) that
 *        it is not found and when the machine goes on without WiFi.
 * @param savedSsid the saved home WiFi, nullptr or "" on the first setup
 */
inline void roundDisplayWifiSetup(const char* ssid, const char* password, const char* savedSsid = nullptr, const unsigned long offlineAfterS = 60) {
    if (roundTft == nullptr) {
        return;
    }

    if (roundPower.asleep()) {
        roundTft->wakeup();
        roundBandFilter.invalidate();
    }

    // WIFI: URI as phone cameras read it; \ ; , : " in name or password need a backslash
    static char qr[160];
    static char line1[64];
    static char line2[64];
    size_t n = static_cast<size_t>(snprintf(qr, sizeof(qr), "WIFI:T:WPA;S:"));

    for (const char* part : {ssid, ";P:", password, ";;"}) {
        const bool escape = part == ssid || part == password;

        for (const char* c = part; *c != '\0' && n + 3 < sizeof(qr); ++c) {
            if (escape && std::strchr("\\;,:\"", *c) != nullptr) {
                qr[n++] = '\\';
            }

            qr[n++] = *c;
        }
    }

    qr[n] = '\0';

    const bool german = config.get<int>("display.language") == 0;
    static char title[64];
    const unsigned long minutes = (offlineAfterS + 59) / 60;

    snprintf(title, sizeof(title), "%s", german ? "WLAN EINRICHTEN" : "WIFI SETUP");

    if (savedSsid == nullptr || *savedSsid == '\0') {
        snprintf(line1, sizeof(line1), "%s %s", german ? "WLAN:" : "WiFi:", ssid);
        snprintf(line2, sizeof(line2), "%s %s", german ? "Passwort:" : "Password:", password);
    }
    else {
        snprintf(line1, sizeof(line1), "%s %s", savedSsid, german ? "nicht erreichbar" : "not found");
        snprintf(line2, sizeof(line2), german ? "Sonst in %lu Min. ohne WLAN" : "Else offline in %lu min", minutes);
    }

    rd::Message m;
    m.title = title;
    m.line1 = line1;
    m.line2 = line2;
    m.qr = qr;
    roundUi.showMessage(m);
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
    ROUND_TIMING_DO(Serial.printf("TIMING heap before display %u\n", static_cast<unsigned>(ESP.getFreeHeap())));
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
    ROUND_TIMING_DO(Serial.printf("TIMING heap after panel init %u\n", static_cast<unsigned>(ESP.getFreeHeap())));
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
    static unsigned long lastModel = 0;
    static unsigned long offlineNoticeStart = 0;

    if (roundTft == nullptr || u8g2 == nullptr) {
        return;
    }

    const unsigned long now = millis();

    // A frame in progress: only its next band, no new machine state, so the picture shows one moment
    if (roundUi.frameOpen()) {
        roundDisplayStep(now);
        return;
    }

    // Look at the machine every 50 ms; during transitions as often as frames are due
    if (!roundUi.animating(now) && !roundPower.closing() && now - lastModel < kRoundModelInterval) {
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

    // Display off (standby) and on again: iris closes before the panel sleeps, opens after waking up
    switch (roundPower.update(u8g2->sleepRequested(), roundUi, now)) {
        case rd::PowerSequencer::Action::Sleep:
            roundTft->sleep();
            roundBandFilter.invalidate();
            return;
        case rd::PowerSequencer::Action::Wake:
            roundTft->wakeup();
            roundBandFilter.invalidate();
            break;
        default:
            break;
    }

    if (roundPower.asleep()) {
        return;
    }

    {
        ROUND_TIME(Model);
        roundUi.update(roundDisplayModel(), now);
    }

    // Like the OLED: skip loops where other slow work runs, but never wait longer than 500 ms
    const bool busy = websiteUpdateRunning || mqttUpdateRunning || hassioUpdateRunning || temperatureUpdateRunning;

    if (roundUi.needsRedraw(now) && (!busy || now - lastDisplayUpdate > 500)) {
        displayUpdateRunning = true;
        roundDisplayStep(now); // first band, the others in the next loop iterations
        lastDisplayUpdate = now;
    }
}
