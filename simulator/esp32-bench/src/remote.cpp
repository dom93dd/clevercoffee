/**
 * @file remote.cpp
 *
 * @brief The round display driven by the simulator on the computer: the simulator sends the machine
 *        state over USB (simulator/src/RemoteLink.h), this program draws it with the firmware's UI code,
 *        band by band as roundDisplayLoop() does. So the real panel shows what the keys in the
 *        simulator window do, at the chip's own speed.
 *
 * ESP32 DevKitC + display only, USB power; wiring as in src/display/roundDisplayDevice.h.
 * Flash: pio run -e remote -t upload; then on the computer: simulator/run.sh --display auto
 */

#include <Arduino.h>

#include "RemoteLink.h"
#include "roundDisplayDevice.h"

#include <RoundDisplayControl.h>
#include <RoundDisplayUi.h>

namespace {
    constexpr int kBandHeight = 40;

    RoundTft tft;
    lgfx::LGFX_Sprite bands[2];
    rd::BandFilter filter;
    rd::RoundUi ui;
    rd::PowerSequencer power;
    sim::link::Parser parser;
    sim::link::TextMessage text;

    rd::Model model;
    bool sleepWanted = false;
    bool sending = false; // SPI transaction open while a frame is drawn band by band
    uint32_t models = 0;
    uint32_t frames = 0;
    uint32_t slowestUs = 0;
    uint32_t lastStatus = 0;

    void push(lgfx::LGFX_Sprite& band, const int top) {
        if (!filter.changed(top / band.height(), static_cast<const uint16_t*>(band.getBuffer()), band.width() * band.height())) {
            tft.waitDMA();
            return;
        }

        tft.pushImageDMA(0, top, band.width(), band.height(), static_cast<lgfx::swap565_t*>(band.getBuffer()));
    }

    /** One band of the current frame, as roundDisplayStep() in the firmware */
    void step(const uint32_t now) {
        const uint32_t start = micros();

        if (!sending) {
            tft.startWrite();
            sending = true;
        }

        if (ui.renderBand(bands, 2, push, now)) {
            tft.endWrite();
            sending = false;
            ++frames;
        }

        slowestUs = std::max(slowestUs, static_cast<uint32_t>(micros() - start));
    }

    void receive() {
        while (Serial.available() > 0) {
            if (!parser.push(static_cast<uint8_t>(Serial.read()))) {
                continue;
            }

            const auto& p = parser.payload();

            switch (parser.type()) {
                case sim::link::Model:
                    if (sim::link::decodeModel(p.data(), p.size(), model)) {
                        ++models;
                    }
                    break;
                case sim::link::Text:
                    {
                        sim::link::TextMessage incoming;

                        if (sim::link::decodeText(p.data(), p.size(), incoming) && !(incoming == text)) {
                            text = incoming;

                            if (text.empty()) {
                                ui.clearMessage();
                            }
                            else {
                                ui.showMessage(text.message()); // points into text, which lives on
                            }
                        }
                        break;
                    }
                case sim::link::Play:
                    if (p.size() == 1 && p[0] <= static_cast<uint8_t>(rd::Animation::Close)) {
                        ui.play(static_cast<rd::Animation>(p[0]), millis());
                    }
                    break;
                case sim::link::Power:
                    sleepWanted = p.size() == 1 && p[0] != 0;
                    break;
                default:
                    break;
            }
        }
    }
} // namespace

void setup() {
    Serial.setRxBufferSize(4096);
    Serial.begin(sim::link::kBaud);

    if (!rd::RoundUi::begin() || !tft.init()) {
        return;
    }

    tft.fillScreen(TFT_BLACK);

    for (auto& b : bands) {
        b.setColorDepth(16);
        b.createSprite(rd::RoundUi::kWidth, kBandHeight);
    }

    ui.setBrand("DOMS COFFEE");
    text = {"SIMULATOR", "Warte auf den", "Simulator am Mac"};
    ui.showMessage(text.message());
    ui.update(model, millis());
    ui.play(rd::Animation::Intro, millis());
}

void loop() {
    receive();
    const uint32_t now = millis();

    // Frame in progress: only its next band, the picture shows one moment (as in the firmware)
    if (ui.frameOpen()) {
        step(now);
        return;
    }

    switch (power.update(sleepWanted, ui, now)) {
        case rd::PowerSequencer::Action::Sleep:
            tft.sleep();
            filter.invalidate();
            break;
        case rd::PowerSequencer::Action::Wake:
            tft.wakeup();
            filter.invalidate();
            break;
        default:
            break;
    }

    if (!power.asleep()) {
        ui.update(model, now);

        if (ui.needsRedraw(now)) {
            step(now);
        }
    }

    // Report to the simulator once a second
    if (now - lastStatus >= 1000) {
        Serial.printf("STATUS %u %u %u %u\n", static_cast<unsigned>(frames), static_cast<unsigned>(models), static_cast<unsigned>(parser.bad), static_cast<unsigned>((slowestUs + 500) / 1000));
        frames = 0;
        slowestUs = 0;
        lastStatus = now;
    }
}
