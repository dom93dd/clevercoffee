/**
 * @file demo.cpp
 *
 * @brief Display demo for the bench: ESP32 DevKitC and the round GC9A01 display, nothing else
 *        (no sensors, no WiFi, powered by USB). Shows a colour test picture, then every simulator
 *        scenario for 4 seconds with its animations running, in German and English by turns.
 *
 * Wiring and panel settings are the firmware's (src/display/roundDisplayDevice.h):
 *   SCL/SCK -> IO14, SDA/MOSI -> IO13, CS -> IO15, DC -> IO4, RST -> IO5, VCC -> 3V3, GND -> GND, BLK -> 3V3
 *
 * Serial (115200 baud) prints the scenario and the real drawing time on the chip:
 *   DEMO <scenario> <caption>
 *   DEMO <frames> frames, slowest <us> us incl. SPI, <bands> bands sent per frame
 *
 * Build and flash: pio run -e demo -t upload -t monitor
 */

#include <Arduino.h>

#include "SimSupport.h"
#include "roundDisplayDevice.h"

#include <RoundDisplayControl.h>

namespace {
    constexpr int kBandHeight = 40;
    constexpr uint32_t kShowMs = 4000;

    RoundTft tft;
    lgfx::LGFX_Sprite bands[2];
    rd::BandFilter filter;
    int sentBands = 0;

    void push(lgfx::LGFX_Sprite& band, const int top) {
        if (!filter.changed(top / band.height(), static_cast<const uint16_t*>(band.getBuffer()), band.width() * band.height())) {
            tft.waitDMA();
            return;
        }

        tft.pushImageDMA(0, top, band.width(), band.height(), static_cast<lgfx::swap565_t*>(band.getBuffer()));
        ++sentBands;
    }

    /** One frame as the firmware draws it; returns the time in microseconds including the transfer */
    uint32_t frame(rd::RoundUi& ui, const uint32_t now) {
        const uint32_t start = micros();
        tft.startWrite();
        ui.render(bands, 2, now, push);
        tft.endWrite();
        return micros() - start;
    }

    /**
     * Colours and greys of the UI as stripes (test plan C2): black must be black (else panel.invert),
     * amber must not look blue (else rgb_order), the dark greys must be visible but subdued.
     */
    void testPicture() {
        struct Stripe {
                uint32_t rgb;
                const char* name;
        };
        const Stripe stripes[] = {
            {0x000000, "schwarz"},   {0x222224, "Ringbahn"}, {0x56565C, "blassgrau"}, {0x8E8E94, "grau"}, {0xF2F2F4, "weiss"},
            {0xFF9226, "bernstein"}, {0x34D399, "gruen"},    {0x7DBEFF, "blau"},      {0xEF4444, "rot"},  {0xF0B45C, "crema"},
        };
        const int h = 240 / static_cast<int>(sizeof(stripes) / sizeof(stripes[0]));
        tft.startWrite();

        for (size_t i = 0; i < sizeof(stripes) / sizeof(stripes[0]); ++i) {
            tft.fillRect(0, static_cast<int32_t>(i) * h, 240, h, lgfx::color888(stripes[i].rgb >> 16 & 0xFF, stripes[i].rgb >> 8 & 0xFF, stripes[i].rgb & 0xFF));
            printf("DEMO test picture stripe %u: %s\n", static_cast<unsigned>(i + 1), stripes[i].name);
        }

        tft.endWrite();
        delay(6000);
    }
} // namespace

void setup() {
    Serial.begin(115200);
    delay(200);

    if (!rd::RoundUi::begin() || !tft.init()) {
        printf("DEMO ERROR fonts or panel\n");
        return;
    }

    for (auto& b : bands) {
        b.setColorDepth(16);
        b.createSprite(rd::RoundUi::kWidth, kBandHeight);
    }

    printf("DEMO start, free heap %u\n", static_cast<unsigned>(ESP.getFreeHeap()));
    testPicture();
}

void loop() {
    static bool english = false;
    const auto all = sim::scenarios();

    for (const auto& sc : all) {
        FakeMachine machine;
        machine.language = english ? rd::Language::English : rd::Language::German;
        machine.reset();
        rd::RoundUi ui;
        ui.setBrand(sim::kDefaultBrand);
        sim::gSimulatedMs = 0;
        sc.setup(machine, ui);
        const uint32_t at = sim::gSimulatedMs > 0 ? sim::gSimulatedMs + sc.atMs : sc.atMs;
        printf("DEMO %s %s\n", sc.name, sc.caption);

        filter.invalidate();
        uint32_t slowest = 0;
        uint32_t frames = 0;
        sentBands = 0;
        const uint32_t begin = millis();

        while (millis() - begin < kShowMs) {
            const uint32_t now = at + (millis() - begin); // animations run on from the scenario's moment
            ui.update(machine.model(), now);

            if (ui.needsRedraw(now)) {
                slowest = std::max(slowest, frame(ui, now));
                ++frames;
            }

            delay(1);
        }

        printf("DEMO %u frames, slowest %u us incl. SPI, %.1f bands sent per frame\n", static_cast<unsigned>(frames), static_cast<unsigned>(slowest), frames > 0 ? static_cast<double>(sentBands) / frames : 0.0);
    }

    english = !english;
}
