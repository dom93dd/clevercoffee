/**
 * @file RoundDisplayUi.cpp
 */

#include "RoundDisplayUi.h"

#include "RoundDisplayFonts.h"
#include "RoundDisplayFormat.h"
#include "RoundDisplayStrings.h"
#include "RoundDisplayTheme.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <initializer_list>

namespace rd {

    using namespace theme;

    namespace {

        constexpr float kAmbient = 20.0f;           // start of the heating gauge
        constexpr float kReadyScale = 5.0f;         // the ready gauge shows setpoint +- 5 K
        constexpr float kHeatingThreshold = 5.0f;   // below setpoint - 5 K the heating screen is shown (as the OLED heating logo)
        constexpr float kRecoveryThreshold = 15.0f; // once warm, only a drop this large switches back to the heating screen
        constexpr float kHysteresis = 0.3f;
        constexpr float kStopwatchScale = 30.0f;    // seconds per revolution without a target time

        // Transition lengths in ms
        constexpr uint32_t kIntroMs = 1700;
        constexpr uint32_t kRevealMs = 900;
        constexpr uint32_t kCloseMs = 800;
        constexpr float kIrisMax = 132.0f; // radius that uncovers the whole round screen incl. the soft edge

        uint32_t animationLength(const Animation a) {
            switch (a) {
                case Animation::Intro:
                    return kIntroMs;
                case Animation::Reveal:
                    return kRevealMs;
                case Animation::Close:
                    return kCloseMs;
                default:
                    return 0;
            }
        }

        bool isAlarm(const Screen s) {
            return s == Screen::EmergencyStop || s == Screen::SensorError;
        }

        /** Screens whose big temperature counts up when revealed */
        bool countsUp(const Screen s) {
            return s == Screen::Heating || s == Screen::Ready || s == Screen::Steam || s == Screen::PidDisabled;
        }

        uint32_t hashAdd(uint32_t h, const int32_t v) {
            // FNV-1a over the four bytes
            for (int i = 0; i < 4; ++i) {
                h ^= static_cast<uint32_t>(v) >> (i * 8) & 0xFF;
                h *= 16777619u;
            }

            return h;
        }

        int32_t q(const float v, const float step) {
            return static_cast<int32_t>(std::lround(v / step));
        }

        float readyAngle(const float temperature, const float setpoint) {
            return clampf((temperature - setpoint) / kReadyScale, -1.0f, 1.0f) * kGaugeEnd;
        }

        void drawMarker(Painter& p, const float angle, const Color c) {
            const float x = Painter::px(kCx, kRingRadius, angle);
            const float y = Painter::py(kCy, kRingRadius, angle);
            p.disc(x, y, 9.0f, kBackground);
            p.disc(x, y, 6.5f, c);
        }

        void drawNoWifiIcon(Painter& p, const float x, const float y, const Color c) {
            // Arcs around a point at the bottom, crossed out
            const float baseY = y + 6.0f;

            for (int i = 0; i < 3; ++i) {
                p.arc(x, baseY, 4.5f + static_cast<float>(i) * 4.0f, 1.9f, -45.0f, 45.0f, c);
            }

            p.disc(x, baseY - 0.5f, 1.6f, c);
            p.line(x - 9.0f, y - 8.0f, x + 9.0f, y + 7.0f, 1.9f, c);
        }

        /** Resistor symbol (lead, zigzag, lead) as printed above the heater lamp of the Orione */
        void drawHeaterIcon(Painter& p, const float x, const float y, const Color c) {
            constexpr float points[][2] = {{-9.0f, 0.0f}, {-5.5f, 0.0f}, {-4.4f, -3.8f}, {-2.2f, 3.8f}, {0.0f, -3.8f}, {2.2f, 3.8f}, {4.4f, -3.8f}, {5.5f, 0.0f}, {9.0f, 0.0f}};
            constexpr int count = sizeof(points) / sizeof(points[0]);

            for (int i = 1; i < count; ++i) {
                p.line(x + points[i - 1][0], y + points[i - 1][1], x + points[i][0], y + points[i][1], 1.6f, c);
            }
        }

        void drawCupIcon(Painter& p, const float x, const float y, const Color c) {
            const float w = 1.7f;
            p.line(x - 7.0f, y - 4.5f, x + 7.0f, y - 4.5f, w, c);
            p.line(x - 6.5f, y - 4.5f, x - 4.5f, y + 4.5f, w, c);
            p.line(x + 6.5f, y - 4.5f, x + 4.5f, y + 4.5f, w, c);
            p.line(x - 4.5f, y + 4.5f, x + 4.5f, y + 4.5f, w, c);
            p.arc(x + 7.0f, y - 1.0f, 3.0f, w, 20.0f, 160.0f, c);
        }

        void drawWarningIcon(Painter& p, const float cx, const float cy, const float size, const Color c) {
            const float h = size * 0.87f;
            const float top = cy - h * 0.55f;
            const float bottom = cy + h * 0.45f;
            const float w = size * 0.1f;
            p.line(cx, top, cx - size * 0.5f, bottom, w, c);
            p.line(cx - size * 0.5f, bottom, cx + size * 0.5f, bottom, w, c);
            p.line(cx + size * 0.5f, bottom, cx, top, w, c);
            p.line(cx, top + h * 0.3f, cx, bottom - h * 0.32f, w, c);
            p.disc(cx, bottom - h * 0.16f, w * 0.62f, c);
        }

        void drawDropIcon(Painter& p, const float cx, const float cy, const float size, const Color c) {
            // Circle at the bottom, two tangents meeting in the tip
            const float r = size * 0.32f;
            const float centerY = cy + size * 0.18f;
            const float tipY = cy - size * 0.5f;
            const float w = size * 0.085f;
            const float d = centerY - tipY;
            const float tangent = std::asin(r / d) * 57.29578f; // angle between the axis and a tangent, seen from the tip
            const float a = 90.0f - tangent;                    // touch points on the circle, measured from 12 o'clock
            p.arc(cx, centerY, r, w, a, 360.0f - a, c);
            p.line(cx, tipY, Painter::px(cx, r, a), Painter::py(centerY, r, a), w, c);
            p.line(cx, tipY, Painter::px(cx, r, -a), Painter::py(centerY, r, -a), w, c);
        }

        void drawCheckIcon(Painter& p, const float cx, const float cy, const float size, const Color c) {
            const float w = size * 0.13f;
            p.line(cx - size * 0.45f, cy, cx - size * 0.12f, cy + size * 0.32f, w, c);
            p.line(cx - size * 0.12f, cy + size * 0.32f, cx + size * 0.45f, cy - size * 0.3f, w, c);
        }

    } // namespace

    const char* screenName(const Screen s) {
        switch (s) {
            case Screen::Boot:
                return "Boot";
            case Screen::Message:
                return "Message";
            case Screen::Heating:
                return "Heating";
            case Screen::Ready:
                return "Ready";
            case Screen::Brew:
                return "Brew";
            case Screen::Flush:
                return "Flush";
            case Screen::HotWater:
                return "HotWater";
            case Screen::Steam:
                return "Steam";
            case Screen::Backflush:
                return "Backflush";
            case Screen::WaterTankEmpty:
                return "WaterTankEmpty";
            case Screen::Standby:
                return "Standby";
            case Screen::PidDisabled:
                return "PidDisabled";
            case Screen::EmergencyStop:
                return "EmergencyStop";
            case Screen::SensorError:
                return "SensorError";
        }

        return "?";
    }

    bool RoundUi::begin() {
        return fonts::load();
    }

    void RoundUi::showMessage(const Message& message) {
        message_ = message;
        hasMessage_ = true;
        screen_ = Screen::Message;
        signature_ = computeSignature(lastDrawMs_);
    }

    void RoundUi::clearMessage() {
        hasMessage_ = false;
    }

    void RoundUi::update(const Model& model, const uint32_t nowMs) {
        model_ = model;

        // Heating screen while warming up from cold. Once warm, the dip after a shot stays on the
        // zoomed ready gauge instead of switching scales for a few seconds.
        switch (model_.mode) {
            case Mode::Init:
            case Mode::Standby:
            case Mode::PidDisabled:
            case Mode::EmergencyStop:
            case Mode::SensorError:
                warmedUp_ = false;
                break;
            default:
                break;
        }

        const float deviation = model_.temperature - model_.setpoint;
        const float threshold = warmedUp_ ? kRecoveryThreshold : kHeatingThreshold;

        // Hysteresis so the screen and the label do not flicker at the thresholds
        if (heating_) {
            heating_ = deviation < -threshold + kHysteresis;
        }
        else {
            heating_ = deviation < -threshold;
        }

        if (!heating_ && model_.mode == Mode::Normal) {
            warmedUp_ = true;
        }

        const float band = std::max(model_.readyBand, 0.1f);

        if (ready_) {
            ready_ = std::fabs(deviation) <= band + kHysteresis;
        }
        else {
            ready_ = std::fabs(deviation) <= band;
        }

        const Screen previous = screen_;
        screen_ = selectScreen(model_);

        if (isAlarm(screen_)) {
            animation_ = Animation::None; // alarms show up at once
        }
        else if (drawnOnce_ && screen_ != previous && animation_ != Animation::Intro) {
            if (screen_ == Screen::Standby) {
                play(Animation::Close, nowMs);
                closingScreen_ = previous;
            }
            else if (previous == Screen::Standby || previous == Screen::Boot || previous == Screen::Message) {
                if (screen_ != Screen::Message) {
                    play(Animation::Reveal, nowMs);
                }
            }
        }

        if (animation_ != Animation::None && !animating(nowMs)) {
            animation_ = Animation::None;
        }

        signature_ = computeSignature(nowMs);
    }

    void RoundUi::play(const Animation animation, const uint32_t nowMs) {
        animation_ = animation;
        animationStart_ = nowMs;
        closingScreen_ = screen_;
    }

    bool RoundUi::animating(const uint32_t nowMs) const {
        return animation_ != Animation::None && nowMs - animationStart_ < animationLength(animation_);
    }

    float RoundUi::animationProgress(const uint32_t nowMs) const {
        const uint32_t length = animationLength(animation_);
        return length == 0 ? 1.0f : std::min(1.0f, static_cast<float>(nowMs - animationStart_) / static_cast<float>(length));
    }

    Screen RoundUi::selectScreen(const Model& m) const {
        // Safety first: alarms win over everything else, messages included
        switch (m.mode) {
            case Mode::EmergencyStop:
                return Screen::EmergencyStop;
            case Mode::SensorError:
                return Screen::SensorError;
            default:
                break;
        }

        if (hasMessage_) {
            return Screen::Message;
        }

        if (m.brewTimerVisible) {
            return Screen::Brew;
        }

        switch (m.mode) {
            case Mode::Init:
                return Screen::Boot;
            case Mode::ManualFlush:
                return Screen::Flush;
            case Mode::HotWater:
                return Screen::HotWater;
            case Mode::Steam:
                return Screen::Steam;
            case Mode::Backflush:
                return Screen::Backflush;
            case Mode::WaterTankEmpty:
                return Screen::WaterTankEmpty;
            case Mode::Standby:
                return Screen::Standby;
            case Mode::PidDisabled:
                return Screen::PidDisabled;
            case Mode::Brew:
                return Screen::Brew;
            default:
                return heating_ ? Screen::Heating : Screen::Ready;
        }
    }

    uint32_t RoundUi::computeSignature(const uint32_t nowMs) const {
        const Model& m = model_;
        uint32_t h = 2166136261u;

        h = hashAdd(h, static_cast<int32_t>(screen_));
        h = hashAdd(h, static_cast<int32_t>(animation_));

        if (animating(nowMs)) {
            h = hashAdd(h, static_cast<int32_t>((nowMs - animationStart_) / animationFrameIntervalMs));
        }

        h = hashAdd(h, static_cast<int32_t>(m.language));
        h = hashAdd(h, ready_);

        if (screen_ == Screen::Message) {
            // The firmware reuses its text buffers, so hash the text, not the pointers
            for (const char* text : {message_.title, message_.line1, message_.line2, message_.line3}) {
                for (const char* c = text; c != nullptr && *c != '\0'; ++c) {
                    h = hashAdd(h, *c);
                }

                h = hashAdd(h, 0);
            }

            return h;
        }

        h = hashAdd(h, q(m.temperature, 0.1f));
        h = hashAdd(h, q(m.setpoint, 0.1f));
        h = hashAdd(h, q(m.heaterPercent, 2.0f));
        h = hashAdd(h, static_cast<int32_t>(m.brewPhase));
        h = hashAdd(h, q(m.brewTime, 0.1f));
        h = hashAdd(h, q(m.brewTargetTime, 0.1f));
        h = hashAdd(h, q(m.lastBrewTime, 0.1f));
        h = hashAdd(h, q(m.flushTime, 0.1f));
        h = hashAdd(h, q(m.hotWaterTime, 0.1f));
        h = hashAdd(h, m.scaleEnabled | m.scaleFault << 1 | m.bleScale << 2 | m.bleScaleConnected << 3);
        h = hashAdd(h, q(m.brewWeight, 0.1f));
        h = hashAdd(h, q(m.brewTargetWeight, 0.1f));
        h = hashAdd(h, static_cast<int32_t>(m.backflushPhase) | m.backflushCycle << 8 | m.backflushCycles << 16);
        h = hashAdd(h, m.offlineMode | m.wifiConnected << 1 | m.wifiBars << 2 | m.mqttEnabled << 5 | m.mqttConnected << 6);
        h = hashAdd(h, q(m.emergencyResetTemp, 0.1f));

        if (screen_ == Screen::EmergencyStop || screen_ == Screen::SensorError) {
            h = hashAdd(h, static_cast<int32_t>(nowMs / 500 % 2)); // blinking ring
        }

        return h;
    }

    bool RoundUi::needsRedraw(const uint32_t nowMs) const {
        if (!drawnOnce_) {
            return true;
        }

        const uint32_t since = nowMs - lastDrawMs_;

        if (since < (animating(nowMs) ? animationFrameIntervalMs : minFrameIntervalMs)) {
            return false;
        }

        return signature_ != drawnSignature_ || since >= maxFrameIntervalMs;
    }

    void RoundUi::draw(Painter& p, const uint32_t nowMs) const {
        p.clear(kBackground);
        view_ = model_;

        const bool running = animating(nowMs);
        const float t = animationProgress(nowMs);

        if (running && animation_ == Animation::Intro) {
            drawIntro(p, t);
            return;
        }

        if (running && animation_ == Animation::Close) {
            // The iris closes over the screen that was shown before, starting right at the edge of the glass
            const float r = (1.0f - easeInOutCubic(phase(t, 0.0f, 0.7f))) * 121.0f;

            if (r > 12.0f) {
                drawScreen(p, closingScreen_, nowMs);
                drawIris(p, r, std::min(1.0f, t * 5.0f));
            }
            else {
                // Only a glowing dot is left; it lights up once more and fades out
                const float fade = 1.0f - easeInOutCubic(phase(t, 0.72f, 1.0f));
                p.disc(kCx, kCy, 10.0f, mix(kBackground, kBrewDark, 0.75f * fade));
                p.disc(kCx, kCy, 4.5f, mix(kBackground, mix(kBrew, kText, 0.5f), fade));
            }
            return;
        }

        if (running && animation_ == Animation::Reveal) {
            // Values roll up from zero while the iris opens
            if (countsUp(screen_)) {
                view_.temperature = model_.temperature * easeOutCubic(t);
            }

            drawScreen(p, screen_, nowMs);
            drawIris(p, easeOutCubic(t) * kIrisMax, 1.0f - phase(t, 0.6f, 1.0f));
            return;
        }

        drawScreen(p, screen_, nowMs);
    }

    void RoundUi::drawScreen(Painter& p, const Screen screen, const uint32_t nowMs) const {
        switch (screen) {
            case Screen::Boot:
                drawBoot(p);
                break;
            case Screen::Message:
                drawMessage(p);
                break;
            case Screen::Heating:
                drawTemperatureGauge(p, true);
                break;
            case Screen::Ready:
                drawTemperatureGauge(p, false);
                break;
            case Screen::Brew:
                drawBrew(p);
                break;
            case Screen::Flush:
                drawStopwatch(p, strings(view_.language).flush, view_.flushTime);
                break;
            case Screen::HotWater:
                drawStopwatch(p, strings(view_.language).hotWater, view_.hotWaterTime);
                break;
            case Screen::Steam:
                drawSteam(p);
                break;
            case Screen::Backflush:
                drawBackflush(p);
                break;
            case Screen::WaterTankEmpty:
                drawWaterTankEmpty(p);
                break;
            case Screen::Standby:
                drawStandby(p);
                break;
            case Screen::PidDisabled:
                drawPidDisabled(p);
                break;
            case Screen::EmergencyStop:
                drawAlarm(p, false, nowMs);
                break;
            case Screen::SensorError:
                drawAlarm(p, true, nowMs);
                break;
        }
    }

    void RoundUi::drawIris(Painter& p, const float radius, const float glow) const {
        p.mask(kCx, kCy, radius, 10.0f);

        // Crema colored rim that lights up the edge of the opening
        if (glow > 0.0f && radius > 2.0f && radius < 112.0f) { // rim and glow stay inside the glass (r < 120)
            p.circle(kCx, kCy, radius + 2.0f, 9.0f, mix(kBackground, kBrewDark, glow * 0.8f));
            p.circle(kCx, kCy, radius, 2.5f, mix(kBackground, kBrew, glow));
        }
    }

    void RoundUi::drawIntro(Painter& p, const float t) const {
        // Instrument self test: the gauge sweeps to full scale while the name fades in, then the
        // fill retracts to the arc of the message screen, so the version message follows seamlessly.
        const float ms = t * static_cast<float>(kIntroMs);
        const float track = phase(ms, 0.0f, 300.0f);
        const float sweep = easeInOutCubic(phase(ms, 100.0f, 750.0f));
        const float retract = easeInOutCubic(phase(ms, 1150.0f, 1700.0f));
        const float name = easeOutCubic(phase(ms, 350.0f, 800.0f));

        p.arc(kCx, kCy, kRingRadius, kRingWidth, kGaugeStart, kGaugeEnd, mix(kBackground, kTrack, track));

        const float fullEnd = kGaugeStart + (kGaugeEnd - kGaugeStart) * sweep;
        const float end = fullEnd - (fullEnd + 30.0f) * retract; // ends at -30 degrees like the message screen

        if (sweep > 0.0f) {
            p.arcGradient(kCx, kCy, kRingRadius, kRingWidth, kGaugeStart, end, kBrewDark, kBrew);
        }

        // A glint runs along the full ring, like light on polished steel
        const float glint = phase(ms, 780.0f, 1180.0f);

        if (glint > 0.0f && glint < 1.0f) {
            constexpr float half = 16.0f;
            const float g = kGaugeStart - half + (kGaugeEnd - kGaugeStart + 2.0f * half) * easeInOutCubic(glint);
            const float a0 = std::max(kGaugeStart, g - half);
            const float a1 = std::min(kGaugeEnd, g + half);
            const auto base = [](const float a) { return mix(kBrewDark, kBrew, (a - kGaugeStart) / (kGaugeEnd - kGaugeStart)); };
            const Color light = mix(base(g), kText, 0.75f);

            if (a0 < g) {
                p.arcGradient(kCx, kCy, kRingRadius, kRingWidth, a0, std::min(g, a1), base(a0), light, false);
            }

            if (g < a1) {
                p.arcGradient(kCx, kCy, kRingRadius, kRingWidth, std::max(g, a0), a1, light, base(a1), false);
            }
        }

        // Glowing head while the fill moves
        const float moving = sweep < 1.0f ? sweep : retract * (1.0f - retract) * 4.0f;

        if (sweep > 0.0f && moving > 0.0f) {
            const float x = Painter::px(kCx, kRingRadius, end);
            const float y = Painter::py(kCy, kRingRadius, end);
            p.disc(x, y, 8.5f, mix(kBackground, kBrewDark, 0.6f * std::min(1.0f, moving * 3.0f))); // 111 + 8.5 stays inside the glass
            p.disc(x, y, 5.5f, mix(kBrew, kText, 0.55f));
        }

        // Name: rises into place, then moves up and dims to where the message screen has it
        const float y = 128.0f + 8.0f * (1.0f - name) - 40.0f * retract;
        const Color c = mix(mix(kBackground, kText, name), kTextDim, retract);
        p.text(fonts::label(), brand_, kCx, y, c);
    }

    // ---------------------------------------------------------------------------------------------
    // Building blocks

    void RoundUi::drawBigValue(Painter& p, const float value, const float y, const Color c, const bool degree, const char* unit) const {
        char buf[16];
        formatNumber(buf, sizeof(buf), value, 1, view_.language);

        // Keep four digit values inside the circle
        if (p.textWidth(fonts::big(), buf) > 176) {
            formatNumber(buf, sizeof(buf), value, 0, view_.language);
        }

        const int w = p.textWidth(fonts::big(), buf);
        float extra = 0.0f;

        if (degree) {
            extra = 16.0f;
        }
        else if (unit != nullptr) {
            extra = 4.0f + static_cast<float>(p.textWidth(fonts::mid(), unit));
        }

        const float left = kCx - (static_cast<float>(w) + extra * 0.5f) * 0.5f;
        p.text(fonts::big(), buf, left, y, c, Align::Left);

        const float right = left + static_cast<float>(w);

        if (degree) {
            p.circle(right + 8.0f, y - static_cast<float>(fonts::kBigDigitHeight) + 6.0f, 5.0f, 2.4f, c);
        }
        else if (unit != nullptr) {
            p.text(fonts::mid(), unit, right + 4.0f, y, mix(c, kBackground, 0.35f), Align::Left);
        }
    }

    bool RoundUi::drawConnectionHint(Painter& p, const float y) const {
        const Model& m = view_;

        // Normal operation stays quiet; offline mode is a deliberate choice and was announced at boot
        if (m.offlineMode || m.wifiConnected) {
            return false;
        }

        const char* text = strings(m.language).noWifi;
        const float w = static_cast<float>(p.textWidth(fonts::text(), text));
        const float left = kCx - (w + 26.0f) * 0.5f;
        drawNoWifiIcon(p, left + 9.0f, y - 7.0f, kTextFaint);
        p.text(fonts::text(), text, left + 26.0f, y, kTextFaint, Align::Left);
        return true;
    }

    void RoundUi::drawHeaterBar(Painter& p) const {
        // Heater output as a short arc in the opening of the gauge, filled from left to right,
        // with the heater symbol printed on the Orione front (resistor zigzag) to its left
        constexpr float from = 148.0f;
        constexpr float to = 198.0f;
        constexpr float symbolAngle = 207.5f;
        const float fill = clampf(view_.heaterPercent / 100.0f, 0.0f, 1.0f);

        p.arc(kCx, kCy, kRingRadius, 4.0f, from, to, kTrack);

        if (fill > 0.005f) {
            p.arc(kCx, kCy, kRingRadius, 4.0f, to - (to - from) * fill, to, kHeat);
        }

        drawHeaterIcon(p, Painter::px(kCx, kRingRadius, symbolAngle), Painter::py(kCy, kRingRadius, symbolAngle), kTextDim);
    }

    void RoundUi::drawTemperatureRow(Painter& p, const float y) const {
        const Model& m = view_;
        char num[16];
        char buf[32];
        formatNumber(num, sizeof(num), m.temperature, 1, m.language);
        snprintf(buf, sizeof(buf), "%s°", num);

        const float deviation = m.temperature - m.setpoint;
        Color c = kTextDim;

        if (std::fabs(deviation) <= std::max(m.readyBand, 0.1f) + kHysteresis) {
            c = kReady;
        }
        else if (deviation < 0) {
            c = kHeat;
        }
        else {
            c = kCool;
        }

        p.text(fonts::text(), buf, kCx, y, c);
    }

    // ---------------------------------------------------------------------------------------------
    // Screens

    void RoundUi::drawBoot(Painter& p) const {
        p.arc(kCx, kCy, kRingRadius, kRingWidth, kGaugeStart, kGaugeEnd, kTrack);
        p.arcGradient(kCx, kCy, kRingRadius, kRingWidth, kGaugeStart, -30.0f, kBrewDark, kBrew);
        p.text(fonts::label(), brand_, kCx, 128.0f, kText);
    }

    namespace {
        struct MessageRow {
                char text[64];
                bool label; // capitals font (short title) instead of the text font
                bool title;
        };

        constexpr int kMaxRows = 7;
        constexpr float kTextRadius = 104.0f; // text stays inside the ring

        bool onlyCapitals(const char* text) {
            for (const char* c = text; *c != '\0'; ++c) {
                if (*c >= 'a' && *c <= 'z') {
                    return false;
                }
            }

            return true;
        }

        /** Line height and first baseline of a block of n centered rows */
        float centeredLineHeight(const int n) {
            return n > 5 ? 22.0f : 24.0f;
        }

        float centeredBaseline(const int i, const int n) {
            return kCy + 6.0f - centeredLineHeight(n) * static_cast<float>(n - 1) * 0.5f + centeredLineHeight(n) * static_cast<float>(i);
        }

        /** Width the circle leaves for a text row with this baseline */
        float rowWidth(const float baseline) {
            const float dy = std::max(std::fabs(baseline - 15.0f - kCy), std::fabs(baseline + 5.0f - kCy));
            return dy >= kTextRadius ? 0.0f : 2.0f * std::sqrt(kTextRadius * kTextRadius - dy * dy);
        }

        /** Breaks text into rows at spaces, row i at most widths[i] wide; returns the new row count */
        int wrapInto(Painter& p, const char* text, const bool title, const float* widths, MessageRow* rows, int count) {
            char line[64] = "";
            const char* word = text;

            const auto flush = [&](const char* content) {
                MessageRow& row = rows[count++];
                snprintf(row.text, sizeof(row.text), "%s", content);
                row.label = false;
                row.title = title;
            };

            while (*word != '\0' && count < kMaxRows) {
                while (*word == ' ') {
                    ++word;
                }

                const char* end = word;

                while (*end != '\0' && *end != ' ') {
                    ++end;
                }

                if (end == word) {
                    break;
                }

                char candidate[64];
                snprintf(candidate, sizeof(candidate), "%s%s%.*s", line, line[0] != '\0' ? " " : "", static_cast<int>(end - word), word);

                if (line[0] == '\0' || static_cast<float>(p.textWidth(fonts::text(), candidate)) <= widths[count]) {
                    snprintf(line, sizeof(line), "%s", candidate);
                }
                else {
                    flush(line);
                    snprintf(line, sizeof(line), "%.*s", static_cast<int>(end - word), word);
                }

                word = end;
            }

            if (line[0] != '\0' && count < kMaxRows) {
                flush(line);
            }

            return count;
        }

        int layoutRows(Painter& p, const Message& message, const float* widths, MessageRow* rows) {
            int count = 0;

            if (message.title != nullptr && message.title[0] != '\0') {
                if (onlyCapitals(message.title) && static_cast<float>(p.textWidth(fonts::label(), message.title)) <= widths[0]) {
                    MessageRow& row = rows[count++];
                    snprintf(row.text, sizeof(row.text), "%s", message.title);
                    row.label = true;
                    row.title = true;
                }
                else {
                    count = wrapInto(p, message.title, true, widths, rows, count);
                }
            }

            for (const char* line : {message.line1, message.line2, message.line3}) {
                if (line != nullptr && line[0] != '\0' && count < kMaxRows) {
                    count = wrapInto(p, line, false, widths, rows, count);
                }
            }

            return count;
        }
    } // namespace

    void RoundUi::drawMessage(Painter& p) const {
        p.arc(kCx, kCy, kRingRadius, kRingWidth, kGaugeStart, kGaugeEnd, kTrack);
        p.arcGradient(kCx, kCy, kRingRadius, kRingWidth, kGaugeStart, -30.0f, kBrewDark, kBrew);

        MessageRow rows[kMaxRows];
        float widths[kMaxRows];

        // Short messages: brand, title and up to two lines as on the boot screens
        std::fill(widths, widths + kMaxRows, 190.0f);
        int count = layoutRows(p, message_, widths, rows);

        if (count <= 3) {
            p.text(fonts::label(), brand_, kCx, 88.0f, kTextDim);
            float y = 122.0f;

            for (int i = 0; i < count; ++i) {
                p.text(rows[i].label ? fonts::label() : fonts::text(), rows[i].text, kCx, y, rows[i].title ? kBrew : kText);
                y += i == 0 && rows[i].title ? 30.0f : 26.0f;
            }

            return;
        }

        // Longer texts: centered block, each row as wide as the circle allows at its height
        int n = 4;

        for (; n <= kMaxRows; ++n) {
            for (int i = 0; i < kMaxRows; ++i) {
                widths[i] = i < n ? rowWidth(centeredBaseline(i, n)) : 0.0f;
            }

            count = layoutRows(p, message_, widths, rows);

            if (count <= n) {
                break;
            }
        }

        n = std::min(n, kMaxRows);

        for (int i = 0; i < count && i < n; ++i) {
            p.text(rows[i].label ? fonts::label() : fonts::text(), rows[i].text, kCx, centeredBaseline(i, n), rows[i].title ? kBrew : kText);
        }
    }

    void RoundUi::drawTemperatureGauge(Painter& p, const bool heating) const {
        const Model& m = view_;
        const Strings& s = strings(m.language);
        const float deviation = m.temperature - m.setpoint;

        p.arc(kCx, kCy, kRingRadius, kRingWidth, kGaugeStart, kGaugeEnd, kTrack);

        const char* label;
        Color accent;

        if (heating) {
            // Progress from room temperature to the setpoint
            const float span = std::max(m.setpoint - kAmbient, 1.0f);
            const float fill = clampf((m.temperature - kAmbient) / span, 0.0f, 1.0f);
            const float head = kGaugeStart + (kGaugeEnd - kGaugeStart) * fill;
            const float readyAt = kGaugeStart + (kGaugeEnd - kGaugeStart) * clampf((span - kHeatingThreshold) / span, 0.0f, 1.0f);

            p.tick(kCx, kCy, readyAt, 94.0f, 100.0f, 1.6f, kTickMinor);
            p.tick(kCx, kCy, kGaugeEnd, 92.0f, 100.0f, 2.4f, kTickMajor);
            p.arcGradient(kCx, kCy, kRingRadius, kRingWidth, kGaugeStart, head, kHeatDark, kHeat);
            drawMarker(p, head, mix(kHeat, kText, 0.35f));

            label = s.heatingUp;
            accent = kHeat;
        }
        else {
            // Zoomed scale: setpoint +- 5 K, setpoint at 12 o'clock
            for (int k = -5; k <= 5; ++k) {
                const float a = static_cast<float>(k) * kGaugeEnd / kReadyScale;

                if (k == 0) {
                    p.tick(kCx, kCy, a, 90.0f, 100.0f, 2.6f, kTickMajor);
                }
                else {
                    p.tick(kCx, kCy, a, 95.0f, 100.0f, 1.6f, kTickMinor);
                }
            }

            if (ready_) {
                label = s.ready;
                accent = kReady;
            }
            else if (deviation < 0) {
                label = s.heating;
                accent = kHeat;
            }
            else {
                label = s.cooling;
                accent = kCool;
            }

            const float a = readyAngle(m.temperature, m.setpoint);
            p.arc(kCx, kCy, kRingRadius, kRingWidth, std::min(0.0f, a), std::max(0.0f, a), mix(accent, kBackground, 0.45f));
            drawMarker(p, a, accent);
        }

        p.text(fonts::label(), label, kCx, kLabelY, accent);
        drawBigValue(p, m.temperature, kValueY, kText, true, nullptr);

        char num[16];
        char buf[48];
        formatNumber(num, sizeof(num), m.setpoint, 1, m.language);
        snprintf(buf, sizeof(buf), "%s %s°", s.setpoint, num);
        p.text(fonts::text(), buf, kCx, kRowAY, kTextDim);

        if (!drawConnectionHint(p, kRowBY) && !heating && m.lastBrewTime > 0.0f) {
            // Duration of the previous shot, handy while dialing in
            formatNumber(num, sizeof(num), m.lastBrewTime, 1, m.language);
            snprintf(buf, sizeof(buf), "%s s", num);
            const float w = static_cast<float>(p.textWidth(fonts::text(), buf));
            const float left = kCx - (w + 24.0f) * 0.5f;
            drawCupIcon(p, left + 8.0f, kRowBY - 7.0f, kTextFaint);
            p.text(fonts::text(), buf, left + 24.0f, kRowBY, kTextFaint, Align::Left);
        }

        drawHeaterBar(p);
    }

    void RoundUi::drawSteam(Painter& p) const {
        const Model& m = view_;
        const Strings& s = strings(m.language);
        const float span = std::max(m.setpoint - kAmbient, 1.0f);
        const float fill = clampf((m.temperature - kAmbient) / span, 0.0f, 1.0f);
        const float head = kGaugeStart + (kGaugeEnd - kGaugeStart) * fill;

        p.arc(kCx, kCy, kRingRadius, kRingWidth, kGaugeStart, kGaugeEnd, kTrack);
        p.tick(kCx, kCy, kGaugeEnd, 92.0f, 100.0f, 2.4f, kTickMajor);
        p.arcGradient(kCx, kCy, kRingRadius, kRingWidth, kGaugeStart, head, mix(kSteam, kBackground, 0.7f), kSteam);
        drawMarker(p, head, mix(kSteam, kText, 0.4f));

        p.text(fonts::label(), s.steam, kCx, kLabelY, kSteam);
        drawBigValue(p, m.temperature, kValueY, kText, true, nullptr);

        char num[16];
        char buf[48];
        formatNumber(num, sizeof(num), m.setpoint, 0, m.language);
        snprintf(buf, sizeof(buf), "%s %s°", s.setpoint, num);
        p.text(fonts::text(), buf, kCx, kRowAY, kTextDim);
        drawConnectionHint(p, kRowBY);
        drawHeaterBar(p);
    }

    void RoundUi::drawBrewLabel(Painter& p, const bool done) const {
        const Model& m = view_;
        const Strings& s = strings(m.language);
        const char* label = s.brew;
        Color labelColor = kBrew;

        if (done) {
            label = s.done;
            labelColor = kReady;
        }
        else if (m.brewPhase == BrewPhase::Preinfusion) {
            label = s.preinfusion;
        }
        else if (m.brewPhase == BrewPhase::PreinfusionPause) {
            label = s.pause;
        }

        if (done) {
            const int w = p.textWidth(fonts::label(), label);
            drawCheckIcon(p, kCx - static_cast<float>(w) * 0.5f - 14.0f, kLabelY - 6.0f, 16.0f, kReady);
        }

        p.text(fonts::label(), label, kCx, kLabelY, labelColor);
    }

    void RoundUi::drawBrew(Painter& p) const {
        // The ring always shows what ends the shot: the weight with brew by weight, otherwise the time
        const Model& m = view_;
        const Strings& s = strings(m.language);
        const bool done = m.brewPhase == BrewPhase::Finished || (m.mode != Mode::Brew && m.brewPhase == BrewPhase::Idle);
        const bool scaleConnected = !m.bleScale || m.bleScaleConnected;
        const bool showWeight = m.scaleEnabled && !m.scaleFault && scaleConnected;

        if (showWeight && m.brewTargetWeight > 0.0f) {
            drawBrewWeightRing(p, done);
            return;
        }

        const float scale = m.brewTargetTime > 0.0f ? m.brewTargetTime : kStopwatchScale;
        const float fill = std::max(0.0f, m.brewTime / scale);

        // Ring: time, one tick every 5 s
        p.circle(kCx, kCy, kRingRadius, kRingWidth, kTrack);

        const int ticks = static_cast<int>(scale / 5.0f);

        for (int k = 1; k < ticks; ++k) {
            p.tick(kCx, kCy, 360.0f * static_cast<float>(k) * 5.0f / scale, 95.0f, 100.0f, 1.6f, kTickMinor);
        }

        p.tick(kCx, kCy, 0.0f, 90.0f, 100.0f, 2.6f, kTickMajor);
        p.arcGradient(kCx, kCy, kRingRadius, kRingWidth, 0.0f, 360.0f * std::min(fill, 1.0f), kBrewDark, kBrew);

        if (fill > 1.0f) {
            // Running over: a second, brighter lap
            p.arc(kCx, kCy, kRingRadius, kRingWidth, 0.0f, 360.0f * std::min(fill - 1.0f, 1.0f), mix(kBrew, kText, 0.5f));
        }

        if (!done) {
            drawMarker(p, 360.0f * std::fmod(fill, 1.0f), mix(kBrew, kText, 0.4f));
        }

        drawBrewLabel(p, done);
        drawBigValue(p, m.brewTime, kValueY, kText, false, "s");

        char num[16];
        char buf[48];

        if (m.scaleEnabled) {
            // Scale without a weight target: weight as a number
            if (m.scaleFault) {
                p.text(fonts::text(), s.scaleFault, kCx, kRowAY, kAlarm);
            }
            else if (!scaleConnected) {
                p.text(fonts::text(), s.scaleDisconnected, kCx, kRowAY, kTextDim);
            }
            else {
                formatNumber(num, sizeof(num), m.brewWeight, 1, m.language);
                snprintf(buf, sizeof(buf), "%s g", num);
                p.text(fonts::mid(), buf, kCx, kRowAY + 2.0f, kWeight);
            }

            return;
        }

        if (m.brewTargetTime > 0.0f) {
            formatNumber(num, sizeof(num), m.brewTargetTime, 0, m.language);
            snprintf(buf, sizeof(buf), "%s %s s", s.target, num);
            p.text(fonts::text(), buf, kCx, kRowAY, kTextDim);
        }

        drawTemperatureRow(p, kRowBY + 2.0f);
    }

    void RoundUi::drawBrewWeightRing(Painter& p, const bool done) const {
        // Brew by weight: the weight ends the shot, so the ring shows the weight up to the target
        const Model& m = view_;
        const Strings& s = strings(m.language);
        const float fill = std::max(0.0f, m.brewWeight / m.brewTargetWeight);

        p.circle(kCx, kCy, kRingRadius, kRingWidth, kTrack);

        for (int k = 1; k < 4; ++k) {
            p.tick(kCx, kCy, 90.0f * static_cast<float>(k), 95.0f, 100.0f, 1.6f, kTickMinor);
        }

        p.tick(kCx, kCy, 0.0f, 90.0f, 100.0f, 2.6f, kTickMajor);
        p.arcGradient(kCx, kCy, kRingRadius, kRingWidth, 0.0f, 360.0f * std::min(fill, 1.0f), kBrewDark, kBrew);

        if (!done) {
            drawMarker(p, 360.0f * std::min(fill, 1.0f), mix(kBrew, kText, 0.4f));
        }

        drawBrewLabel(p, done);
        drawBigValue(p, m.brewWeight, kValueY, kText, false, "g");

        char num[16];
        char buf[48];
        formatNumber(num, sizeof(num), m.brewTime, 1, m.language);
        snprintf(buf, sizeof(buf), "%s s", num);
        p.text(fonts::mid(), buf, kCx, kRowAY + 2.0f, mix(kText, kBackground, 0.25f));

        formatNumber(num, sizeof(num), m.brewTargetWeight, 0, m.language);
        snprintf(buf, sizeof(buf), "%s %s g", s.target, num);
        p.text(fonts::text(), buf, kCx, kRowBY + 4.0f, kTextDim);
    }

    void RoundUi::drawStopwatch(Painter& p, const char* label, const float seconds) const {
        const float fill = std::fmod(std::max(0.0f, seconds), kStopwatchScale) / kStopwatchScale;

        p.circle(kCx, kCy, kRingRadius, kRingWidth, kTrack);

        for (int k = 1; k < 6; ++k) {
            p.tick(kCx, kCy, 60.0f * static_cast<float>(k), 95.0f, 100.0f, 1.6f, kTickMinor);
        }

        p.tick(kCx, kCy, 0.0f, 90.0f, 100.0f, 2.6f, kTickMajor);
        p.arc(kCx, kCy, kRingRadius, kRingWidth, 0.0f, 360.0f * fill, mix(kWater, kBackground, 0.35f));
        drawMarker(p, 360.0f * fill, mix(kWater, kText, 0.4f));

        p.text(fonts::label(), label, kCx, kLabelY, kWater);
        drawBigValue(p, seconds, kValueY, kText, false, "s");
        drawTemperatureRow(p, kRowBY);
    }

    void RoundUi::drawBackflush(Painter& p) const {
        const Model& m = view_;
        const Strings& s = strings(m.language);
        const int cycles = std::max<int>(m.backflushCycles, 1);
        const float slot = 360.0f / static_cast<float>(cycles);
        const float gap = cycles > 1 ? 6.0f : 0.0f;
        const bool running = m.backflushPhase == BackflushPhase::Filling || m.backflushPhase == BackflushPhase::Flushing;

        for (int i = 0; i < cycles; ++i) {
            const float a0 = static_cast<float>(i) * slot + gap * 0.5f;
            const float a1 = static_cast<float>(i + 1) * slot - gap * 0.5f;
            Color c = kTrack;

            if (m.backflushPhase == BackflushPhase::Ending || m.backflushPhase == BackflushPhase::Finished || i + 1 < m.backflushCycle) {
                c = kBrew;
            }
            else if (running && i + 1 == m.backflushCycle) {
                c = mix(kBrew, kBackground, 0.5f);
            }

            p.arc(kCx, kCy, kRingRadius, kRingWidth, a0, a1, c, false);
        }

        p.text(fonts::label(), s.backflush, kCx, kLabelY, kBrew);

        if (running) {
            char buf[16];
            snprintf(buf, sizeof(buf), "%d/%d", m.backflushCycle, m.backflushCycles);
            p.text(fonts::big(), buf, kCx, kValueY, kText);
            p.text(fonts::text(), m.backflushPhase == BackflushPhase::Filling ? s.filling : s.flushing, kCx, kRowAY, kTextDim);
        }
        else {
            const bool ending = m.backflushPhase == BackflushPhase::Ending || m.backflushPhase == BackflushPhase::Finished;
            p.text(fonts::text(), ending ? s.switchOff : s.switchOn, kCx, 124.0f, kText);
            p.text(fonts::text(), ending ? s.toFinish : s.toStart, kCx, 150.0f, kTextDim);
        }
    }

    void RoundUi::drawWaterTankEmpty(Painter& p) const {
        const Model& m = view_;
        const Strings& s = strings(m.language);

        p.circle(kCx, kCy, kRingRadius, kRingWidth, mix(kWater, kBackground, 0.55f));
        drawDropIcon(p, kCx, 84.0f, 52.0f, kWater);
        p.text(fonts::label(), s.waterTank, kCx, 140.0f, kWater);
        p.text(fonts::label(), s.empty, kCx, 164.0f, kWater);
        p.text(fonts::text(), s.refill, kCx, 192.0f, kTextDim);
    }

    void RoundUi::drawStandby(Painter& p) const {
        const Model& m = view_;
        const Strings& s = strings(m.language);
        char num[16];
        char buf[24];

        p.text(fonts::label(), s.standby, kCx, 108.0f, kTextFaint);
        formatNumber(num, sizeof(num), m.temperature, 0, m.language);
        snprintf(buf, sizeof(buf), "%s°", num);
        p.text(fonts::mid(), buf, kCx, 150.0f, kTextFaint);
    }

    void RoundUi::drawPidDisabled(Painter& p) const {
        const Model& m = view_;
        const Strings& s = strings(m.language);

        p.arc(kCx, kCy, kRingRadius, kRingWidth, kGaugeStart, kGaugeEnd, kTrack);
        p.text(fonts::label(), s.pidOff, kCx, kLabelY, kTextDim);
        drawBigValue(p, m.temperature, kValueY, kTextDim, true, nullptr);
        p.text(fonts::text(), s.pidOffHint, kCx, kRowAY, kTextFaint);
    }

    void RoundUi::drawAlarm(Painter& p, const bool sensorError, const uint32_t nowMs) const {
        const Model& m = view_;
        const Strings& s = strings(m.language);
        const bool bright = nowMs / 500 % 2 == 0;

        p.circle(kCx, kCy, kRingRadius, kRingWidth, bright ? kAlarm : kAlarmDark);

        char num[16];
        char buf[48];

        if (sensorError) {
            drawWarningIcon(p, kCx, 64.0f, 42.0f, kAlarm);
            p.text(fonts::label(), s.sensorError, kCx, 114.0f, kAlarm);
            formatNumber(num, sizeof(num), m.temperature, 1, m.language);
            snprintf(buf, sizeof(buf), "%s°", num);
            p.text(fonts::mid(), buf, kCx, 152.0f, kText);
            p.text(fonts::text(), s.heaterOff, kCx, 180.0f, kTextDim);
            p.text(fonts::text(), s.checkSensor, kCx, 202.0f, kTextDim);
        }
        else {
            drawWarningIcon(p, kCx, 52.0f, 34.0f, kAlarm);
            p.text(fonts::label(), s.overTemp, kCx, 94.0f, kAlarm);
            drawBigValue(p, m.temperature, 156.0f, kAlarm, true, nullptr);
            formatNumber(num, sizeof(num), m.emergencyResetTemp, 0, m.language);
            snprintf(buf, sizeof(buf), "%s %s %s°", s.heaterOff, s.heaterOffUntil, num);

            if (p.textWidth(fonts::text(), buf) <= 170) {
                p.text(fonts::text(), buf, kCx, 190.0f, kTextDim);
            }
            else {
                p.text(fonts::text(), s.heaterOff, kCx, 186.0f, kTextDim);
                snprintf(buf, sizeof(buf), "%s %s°", s.heaterOffUntil, num);
                p.text(fonts::text(), buf, kCx, 208.0f, kTextDim);
            }
        }
    }

} // namespace rd
