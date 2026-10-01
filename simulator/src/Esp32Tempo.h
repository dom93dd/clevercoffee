/**
 * @file Esp32Tempo.h
 *
 * @brief Shows the UI at the speed of the ESP32, band by band as the GC9A01 receives it.
 *
 * Each frame is drawn here, the time per band is measured and scaled to the ESP32. Then the
 * firmware's sequence is replayed in time: one band per loop() iteration (roundDisplayStep), wait for
 * the previous DMA transfer, send the band over SPI (only if it changed, as rd::BandFilter does in the
 * firmware), then the rest of loop() runs. A band appears in the picture when its transfer is done,
 * and the next frame starts after the last band. So jerky animations and the top-to-bottom build-up
 * of a frame become visible here first.
 */

#pragma once

#include <LovyanGFX.hpp>
#include <RoundDisplayControl.h>
#include <RoundDisplayUi.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <deque>
#include <vector>

namespace sim {

    /**
     * ESP32 microseconds per microsecond of drawing on this computer. esp32-bench in the QEMU emulator
     * executes 61,000 instructions per microsecond the simulator needs here (median of all scenarios,
     * 47,000-65,000; measured 01.10.2026 on Apple Silicon). Time on the chip: 1.3 cycles per instruction
     * at 240 MHz, own assumption between 1.0 and 1.6 until the real chip is measured.
     */
    constexpr double kEsp32UsPerHostUs = 61000.0 * 1.3 / 240.0;
    constexpr double kBandHashUs = 150.0;   // FNV hash of a 240x40 band on the ESP32 (estimate)
    constexpr double kOtherLoopUs = 1000.0; // the rest of loop() between two bands (assumption until measured)

    class Esp32Tempo {
        public:
            explicit Esp32Tempo(const int bandHeight = 40) {
                panel_.setColorDepth(16);
                panel_.createSprite(rd::RoundUi::kWidth, rd::RoundUi::kHeight);
                panel_.fillScreen(0);

                for (auto& b : bands_) {
                    b.setColorDepth(16);
                    b.createSprite(rd::RoundUi::kWidth, bandHeight);
                }
            }

            /** SPI clock in Hz, 0 = off */
            void setSpiHz(const uint32_t hz) {
                spiHz_ = hz;
                pending_.clear();
                busyUntil_ = 0.0;
                filter_.invalidate();
            }

            uint32_t spiHz() const {
                return spiHz_;
            }

            /** loop() still blocked by the previous frame */
            bool busy(const uint32_t nowMs) const {
                return static_cast<double>(nowMs) < busyUntil_;
            }

            /** Panel content unknown (after sleep): send everything again */
            void invalidate() {
                filter_.invalidate();
            }

            /** Draws a frame and schedules when its bands appear */
            void render(rd::RoundUi& ui, const uint32_t nowMs) {
                // Time per band here: fastest of three runs, as single runs jitter by tens of microseconds
                constexpr int kRuns = 3;
                std::vector<double> bandUs;
                std::vector<std::vector<uint16_t>> pixels;

                for (int run = 0; run < kRuns; ++run) {
                    size_t index = 0;
                    auto last = std::chrono::steady_clock::now();

                    ui.render(bands_, 2, nowMs, [&](lgfx::LGFX_Sprite& band, int) {
                        const auto now = std::chrono::steady_clock::now();
                        const double us = std::chrono::duration<double, std::micro>(now - last).count();

                        if (run == 0) {
                            bandUs.push_back(us);
                            const auto* data = static_cast<const uint16_t*>(band.getBuffer());
                            pixels.emplace_back(data, data + band.width() * band.height());
                        }
                        else {
                            bandUs[index] = std::min(bandUs[index], us);
                        }

                        ++index;
                        last = std::chrono::steady_clock::now();
                    });
                }

                // The firmware's sequence on the chip, in microseconds from the start of the frame
                const double sendUs = spiHz_ > 0 ? static_cast<double>(rd::RoundUi::kWidth * bands_[0].height() * 16) * 1e6 / spiHz_ : 0.0;
                double cpu = 0.0;
                double dmaFree = 0.0;
                double longestBlock = 0.0;
                int sent = 0;

                for (size_t i = 0; i < pixels.size(); ++i) {
                    const double begin = cpu;
                    cpu += bandUs[i] * kEsp32UsPerHostUs + kBandHashUs;

                    if (filter_.changed(static_cast<int>(i), pixels[i].data(), pixels[i].size())) {
                        cpu = std::max(cpu, dmaFree); // pushImageDMA waits for the previous transfer
                        dmaFree = cpu + sendUs;
                        pending_.push_back({static_cast<int>(i) * bands_[0].height(), std::move(pixels[i]), nowMs + dmaFree / 1000.0});
                        ++sent;
                    }
                    else {
                        cpu = std::max(cpu, dmaFree); // waitDMA(): the other buffer is drawn into next
                    }

                    if (i + 1 == pixels.size() || !bandwise) {
                        cpu = i + 1 == pixels.size() ? std::max(cpu, dmaFree) : cpu; // endWrite() waits for the last transfer
                    }

                    if (bandwise || i + 1 == pixels.size()) {
                        longestBlock = std::max(longestBlock, bandwise ? cpu - begin : cpu);
                    }

                    if (bandwise && i + 1 < pixels.size()) {
                        cpu += kOtherLoopUs; // loop() does its other work between two bands
                    }
                }

                busyUntil_ = nowMs + cpu / 1000.0;
                lastFrameMs = static_cast<float>(cpu / 1000.0);
                lastBlockMs = static_cast<float>(longestBlock / 1000.0);
                lastSentBands = sent;
                ++frames_;
            }

            /** Copies the bands that have arrived by now into the panel picture */
            void present(const uint32_t nowMs) {
                while (!pending_.empty() && pending_.front().visibleAtMs <= nowMs) {
                    const Band& b = pending_.front();
                    auto* dst = static_cast<uint16_t*>(panel_.getBuffer()) + b.top * rd::RoundUi::kWidth;
                    const size_t rows = std::min<size_t>(b.pixels.size() / rd::RoundUi::kWidth, rd::RoundUi::kHeight - b.top);
                    std::memcpy(dst, b.pixels.data(), rows * rd::RoundUi::kWidth * sizeof(uint16_t));
                    pending_.pop_front();
                }

                if (nowMs - fpsStart_ >= 1000) {
                    fps = static_cast<float>(frames_) * 1000.0f / static_cast<float>(nowMs - fpsStart_);
                    frames_ = 0;
                    fpsStart_ = nowMs;
                }
            }

            /** What the GC9A01 shows */
            const lgfx::LGFX_Sprite& panel() const {
                return panel_;
            }

            void darken() {
                panel_.fillScreen(0);
                pending_.clear();
            }

            bool bandwise = true;     // one band per loop() as in the firmware; false: whole frame at once
            float lastFrameMs = 0.0f; // from the start of a frame until the last band is shown
            float lastBlockMs = 0.0f; // longest single stop of loop() for the display in that frame
            int lastSentBands = 0;
            float fps = 0.0f;

        private:
            struct Band {
                    int top;
                    std::vector<uint16_t> pixels;
                    double visibleAtMs;
            };

            lgfx::LGFX_Sprite panel_;
            lgfx::LGFX_Sprite bands_[2];
            rd::BandFilter filter_;
            std::deque<Band> pending_;
            uint32_t spiHz_ = 0;
            double busyUntil_ = 0.0;
            int frames_ = 0;
            uint32_t fpsStart_ = 0;
    };

} // namespace sim
