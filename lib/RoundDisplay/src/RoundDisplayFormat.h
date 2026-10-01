/**
 * @file RoundDisplayFormat.h
 *
 * @brief Number formatting and small math helpers of the round display UI.
 */

#pragma once

#include "RoundDisplayModel.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>

namespace rd {

    /** Number with fixed decimals (0..2); German uses a decimal comma. Never prints "-0,0". */
    /** Values beyond this are no measurement but a broken sensor or setting; they show as "--" */
    constexpr float kLargestShownValue = 10000.0f;

    inline void formatNumber(char* out, const size_t size, float value, const int decimals, const Language lang) {
        if (!std::isfinite(value) || std::fabs(value) >= kLargestShownValue) {
            snprintf(out, size, "--");
            return;
        }

        const float half = decimals == 0 ? 0.5f : (decimals == 1 ? 0.05f : 0.005f);

        if (std::fabs(value) < half) {
            value = 0.0f;
        }

        snprintf(out, size, "%.*f", decimals, static_cast<double>(value));

        if (lang == Language::German) {
            for (char* c = out; *c != '\0'; ++c) {
                if (*c == '.') {
                    *c = ',';
                }
            }
        }
    }

    inline float clampf(const float v, const float lo, const float hi) {
        return std::max(lo, std::min(hi, v));
    }

    inline float easeOutCubic(const float t) {
        const float u = 1.0f - t;
        return 1.0f - u * u * u;
    }

    inline float easeInOutCubic(const float t) {
        return t < 0.5f ? 4.0f * t * t * t : 1.0f - std::pow(-2.0f * t + 2.0f, 3.0f) * 0.5f;
    }

    /** 0..1 progress of x through the window [from, to] */
    inline float phase(const float x, const float from, const float to) {
        return clampf((x - from) / (to - from), 0.0f, 1.0f);
    }

} // namespace rd
