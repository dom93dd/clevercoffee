/**
 * @file OrioneFlow.h
 *
 * @brief Flow (g/s) and the first drops of a shot, from the scale's weight as it comes in.
 *
 * The flow is the slope of the weight over the last second, worked out at the scale's own rate
 * (a Bluetooth scale reports 5 to 10 times a second): finer than the shot curve, which keeps a
 * point every half second. Lifting the cup makes the weight drop; that is not a negative flow, the
 * flow is then 0. The first drops are the moment the weight first reaches half a gram. No Arduino
 * dependencies: tested in simulator/test/test_orione.
 */

#pragma once

#include <cmath>
#include <cstdint>

namespace orione {

    class FlowMeter {
        public:
            static constexpr int kSamples = 32;            // a little over 1.5 s at 20 Hz
            static constexpr uint32_t kWindowMs = 1000;    // slope over the last second
            static constexpr uint32_t kMinSpanMs = 300;    // less at the start of a shot: no flow yet
            static constexpr uint32_t kMinStepMs = 50;     // loop() calls far more often than the scale reports
            static constexpr float kFirstDropGrams = 0.5f;

            void start(const uint32_t nowMs) {
                count_ = 0;
                head_ = 0;
                startMs_ = nowMs;
                firstDropMs_ = 0;
                flow_ = 0.0f;
                running_ = true;
            }

            void stop() {
                running_ = false;
            }

            bool running() const {
                return running_;
            }

            void add(const uint32_t nowMs, const float grams) {
                if (!running_ || !std::isfinite(grams)) {
                    return;
                }

                if (count_ > 0 && nowMs - ms_[newest()] < kMinStepMs) {
                    return;
                }

                ms_[head_] = nowMs;
                g_[head_] = grams;
                head_ = (head_ + 1) % kSamples;
                count_ = count_ < kSamples ? count_ + 1 : kSamples;

                if (firstDropMs_ == 0 && grams >= kFirstDropGrams) {
                    firstDropMs_ = nowMs == startMs_ ? nowMs + 1 : nowMs;
                }

                // the newest sample at least a window back, else the oldest one if it spans enough
                int j = -1;

                for (int k = 1; k < count_; ++k) {
                    const int idx = (head_ - 1 - k + 2 * kSamples) % kSamples;

                    if (nowMs - ms_[idx] >= kWindowMs) {
                        j = idx;
                        break;
                    }

                    j = idx;
                }

                if (j < 0 || nowMs - ms_[j] < kMinSpanMs) {
                    flow_ = 0.0f;
                    return;
                }

                const float slope = (grams - g_[j]) / (static_cast<float>(nowMs - ms_[j]) / 1000.0f);
                flow_ = slope > 0.0f ? slope : 0.0f;
            }

            /** g/s */
            float flow() const {
                return flow_;
            }

            /** seconds from the start until the first drops, < 0 while there were none */
            float firstDropSeconds() const {
                return firstDropMs_ == 0 ? -1.0f : static_cast<float>(firstDropMs_ - startMs_) / 1000.0f;
            }

        private:
            int newest() const {
                return (head_ - 1 + kSamples) % kSamples;
            }

            uint32_t ms_[kSamples] = {};
            float g_[kSamples] = {};
            int head_ = 0;
            int count_ = 0;
            uint32_t startMs_ = 0;
            uint32_t firstDropMs_ = 0;
            float flow_ = 0.0f;
            bool running_ = false;
    };

} // namespace orione
