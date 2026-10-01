/**
 * @file main.cpp
 *
 * @brief Measures on the ESP32 how long the round display UI needs per screen.
 *
 * Every simulator scenario is drawn the way the firmware draws: two 240x40 band sprites, one
 * band after the other. Without a panel the push callback only takes the time, so the numbers
 * are pure drawing time; the SPI transfer comes on top on real hardware (overlapping with the
 * drawing thanks to DMA). One line per scenario on the serial port:
 *
 *   BENCH <scenario> us <frame> band_us <slowest band> cycles <frame> crc <checksum of the picture>
 *
 * On a real ESP32 the microseconds are real time. In QEMU with -icount the clock follows the
 * executed instructions instead (run_qemu.sh turns them into an estimate for the real chip).
 *
 * The checksum covers the band buffers exactly as they go to the panel; the simulator prints the
 * same with --crc, so equal sums mean the ESP32 draws exactly the simulator's picture.
 */

#include <Arduino.h>
#include <esp32/rom/crc.h>

#include "SimSupport.h"

#include <RoundDisplayFonts.h>
#include <cstring>
#include <functional>

namespace {
    constexpr int kBandHeight = 40;
    constexpr int kRepeats = 3; // the fastest of a few runs, as caches warm up

    lgfx::LGFX_Sprite bands[2];

    void benchScenario(const sim::Scenario& sc) {
        FakeMachine machine;
        machine.language = rd::Language::German;
        machine.reset();
        rd::RoundUi ui;
        ui.setBrand(sim::kDefaultBrand);
        sim::gSimulatedMs = 0;
        sc.setup(machine, ui);
        const uint32_t at = sim::gSimulatedMs > 0 ? sim::gSimulatedMs + sc.atMs : sc.atMs;
        ui.update(machine.model(), at);

        uint32_t bestFrame = UINT32_MAX;
        uint32_t bestBand = UINT32_MAX;
        uint32_t bestCycles = UINT32_MAX;

        for (int r = 0; r < kRepeats; ++r) {
            uint32_t slowestBand = 0;
            const uint32_t cycles = ESP.getCycleCount();
            const uint32_t start = micros();
            uint32_t last = start;

            ui.render(bands, 2, at, [&](lgfx::LGFX_Sprite&, int) {
                const uint32_t now = micros();
                slowestBand = std::max(slowestBand, now - last);
                last = micros();
            });

            bestFrame = std::min(bestFrame, static_cast<uint32_t>(micros() - start));
            bestCycles = std::min(bestCycles, ESP.getCycleCount() - cycles);
            bestBand = std::min(bestBand, slowestBand);
        }

        uint32_t crc = 0;
        ui.render(bands, 2, at, [&crc](lgfx::LGFX_Sprite& band, int) { crc = crc32_le(crc, static_cast<const uint8_t*>(band.getBuffer()), band.width() * band.height() * 2); });

#ifdef BENCH_DUMP
        if (std::strcmp(sc.name, BENCH_DUMP) == 0) {
            ui.render(bands, 2, at, [](lgfx::LGFX_Sprite& band, const int top) {
                const auto* px = static_cast<const uint16_t*>(band.getBuffer());

                for (int y = 0; y < band.height(); ++y) {
                    printf("DUMP %d ", top + y);

                    for (int x = 0; x < band.width(); ++x) {
                        printf("%04x", px[y * band.width() + x]);
                    }

                    printf("\n");
                }
            });
        }
#endif

        printf("BENCH %s us %u band_us %u cycles %u crc %08x\n", sc.name, static_cast<unsigned>(bestFrame), static_cast<unsigned>(bestBand), static_cast<unsigned>(bestCycles), static_cast<unsigned>(crc));
    }
    /** Time of single drawing operations over the whole screen (6 bands), to see where the time goes */
    void benchPrimitives() {
        using rd::Painter;
        const rd::Color c = rd::rgb(240, 180, 92);
        struct Op {
                const char* name;
                std::function<void(Painter&)> draw;
        };
        const Op ops[] = {
            {"arc-270", [&](Painter& p) { p.arc(120, 120, 111, 9, -135, 135, c); }},
            {"arc-36", [&](Painter& p) { p.arc(120, 120, 111, 4, 162, 198, c); }},
            {"gradient-180", [&](Painter& p) { p.arcGradient(120, 120, 111, 9, -135, 45, c, 0xFFFFFF); }},
            {"circle", [&](Painter& p) { p.circle(120, 120, 111, 9, c); }},
            {"disc-8", [&](Painter& p) { p.disc(60, 60, 8.5f, c); }},
            {"tick", [&](Painter& p) { p.tick(120, 120, 30, 95, 100, 1.6f, c); }},
            {"text-big", [&](Painter& p) { p.text(rd::fonts::big(), "94,1", 120, 139, c); }},
            {"text-small", [&](Painter& p) { p.text(rd::fonts::textSmall(), "Soll 94,0°", 120, 172, c); }},
            {"mask", [&](Painter& p) { p.mask(120, 120, 80, 2); }},
            {"clear", [&](Painter& p) { p.clear(0); }},
        };

        // Cost of the math functions per call (instructions in QEMU = us * 1000 / calls)
        volatile float sink = 0.0f;
        const auto mathCost = [&](const char* name, const std::function<float(float)>& f) {
            const uint32_t start = micros();

            for (int i = 0; i < 10000; ++i) {
                sink = sink + f(static_cast<float>(i) * 0.013f - 50.0f);
            }

            printf("PRIM %s x10000 us %u\n", name, static_cast<unsigned>(micros() - start));
        };
        mathCost("loop-only", [](const float x) { return x; });
        mathCost("sqrtf", [](const float x) { return std::sqrt(std::fabs(x)); });
        mathCost("atan2f", [](const float x) { return std::atan2(x, 37.0f - x); });
        mathCost("fmodf", [](const float x) { return std::fmod(x + 720.0f, 360.0f); });
        mathCost("hypotf", [](const float x) { return std::hypot(x, 12.0f); });
        mathCost("sinf", [](const float x) { return std::sin(x); });
        mathCost("divide", [](const float x) { return 1.0f / (x + 100.5f); });

        for (const auto& op : ops) {
            uint32_t best = UINT32_MAX;

            for (int r = 0; r < kRepeats; ++r) {
                const uint32_t start = micros();

                for (int top = 0; top < 240; top += kBandHeight) {
                    Painter p(bands[0], top);
                    op.draw(p);
                }

                best = std::min(best, static_cast<uint32_t>(micros() - start));
            }

            printf("PRIM %s us %u\n", op.name, static_cast<unsigned>(best));
        }
    }
} // namespace

void setup() {
    Serial.begin(115200);
    delay(200);

    if (!rd::RoundUi::begin()) {
        printf("BENCH ERROR fonts\n");
        return;
    }

    for (auto& b : bands) {
        b.setColorDepth(16);

        if (b.createSprite(rd::RoundUi::kWidth, kBandHeight) == nullptr) {
            printf("BENCH ERROR no RAM for the bands\n");
            return;
        }
    }

    const auto all = sim::scenarios();
    printf("BENCH START %u scenarios, CPU %u MHz, free heap %u\n", static_cast<unsigned>(all.size()), static_cast<unsigned>(getCpuFrequencyMhz()), static_cast<unsigned>(ESP.getFreeHeap()));

    benchPrimitives();

    for (const auto& sc : all) {
        benchScenario(sc);
    }

    printf("BENCH END\n");
}

void loop() {
    delay(1000);
}
