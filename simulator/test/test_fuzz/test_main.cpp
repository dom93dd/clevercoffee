/**
 * @file test_main.cpp
 *
 * @brief Random machine states, values and messages, including NaN, infinity, huge and negative
 *        numbers, broken UTF-8 and jumps of the clock. Whatever the firmware hands over, the UI must
 *
 * - not crash and not run into undefined behaviour (run with pio test -e sanitize to check that),
 * - finish every frame quickly (a loop bound taken from a broken value would hang the ESP32),
 * - draw nothing outside the round glass.
 *
 * The random sequence is fixed, so a failure can be reproduced with the printed case number.
 */

#include "../support/TestSupport.h"

#include "../../src/Png.h"
#include "../../src/SimSupport.h"

#include <RoundDisplayControl.h>
#include <RoundDisplayFormat.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <limits>
#include <random>
#include <string>
#include <vector>

using namespace rd;

namespace {
    std::mt19937 rng(20261001);

    int pick(const int n) {
        return std::uniform_int_distribution<int>(0, n - 1)(rng);
    }

    bool chance(const int percent) {
        return pick(100) < percent;
    }

    /** Mostly sensible values, sometimes everything a broken sensor or config could deliver */
    float value(const float lo, const float hi) {
        constexpr float inf = std::numeric_limits<float>::infinity();

        switch (pick(14)) {
            case 0:
                return std::numeric_limits<float>::quiet_NaN();
            case 1:
                return inf;
            case 2:
                return -inf;
            case 3:
                return 1e30f;
            case 4:
                return -1e30f;
            case 5:
                return 0.0f;
            case 6:
                return -0.0f;
            case 7:
                return std::numeric_limits<float>::denorm_min();
            case 8:
                return 3e9f; // beyond int
            case 9:
                return -50.0f;
            default:
                return std::uniform_real_distribution<float>(lo, hi)(rng);
        }
    }

    Model randomModel() {
        Model m;
        m.mode = static_cast<Mode>(pick(12));
        m.language = chance(50) ? Language::German : Language::English;
        m.temperature = value(-60.0f, 200.0f);
        m.setpoint = value(80.0f, 140.0f);
        m.heaterPercent = value(0.0f, 100.0f);
        m.readyBand = value(0.0f, 2.0f);
        m.emergencyResetTemp = value(90.0f, 140.0f);
        m.brewTimerVisible = chance(60);
        m.brewPhase = static_cast<BrewPhase>(pick(5));
        m.brewTime = value(0.0f, 60.0f);
        m.brewTargetTime = value(0.0f, 60.0f);
        m.lastBrewTime = value(0.0f, 60.0f);
        m.flushTime = value(0.0f, 60.0f);
        m.hotWaterTime = value(0.0f, 60.0f);
        m.scaleEnabled = chance(50);
        m.scaleFault = chance(15);
        m.bleScale = chance(50);
        m.bleScaleConnected = chance(50);
        m.brewWeight = value(-5.0f, 60.0f);
        m.brewTargetWeight = value(0.0f, 60.0f);
        m.backflushPhase = static_cast<BackflushPhase>(pick(5));
        m.backflushCycle = static_cast<uint8_t>(pick(256));
        m.backflushCycles = static_cast<uint8_t>(pick(256));
        m.offlineMode = chance(20);
        m.wifiConnected = chance(70);
        m.wifiBars = static_cast<uint8_t>(pick(256));
        m.mqttEnabled = chance(30);
        m.mqttConnected = chance(50);
        return m;
    }

    /** Text as the firmware might send it: words, umlauts, very long lines, broken UTF-8 */
    std::string randomText() {
        static const char* pieces[] = {"Taring",
                                       "scale,",
                                       "remove",
                                       "any",
                                       "load!",
                                       "....",
                                       "done",
                                       "Bitte",
                                       "nachfüllen",
                                       "ÄÖÜß",
                                       "192.168.178.42",
                                       "\n",
                                       "\n\n",
                                       " ",
                                       "   ",
                                       "Kalibrierung",
                                       "läuft.",
                                       "€",
                                       "😀",
                                       "\xC3",
                                       "\xE2\x82",
                                       "\xFF\xFE",
                                       "\t",
                                       "WLAN-EINRICHTUNG",
                                       "Supercalifragilisticexpialidocious"};
        std::string s;
        const int n = pick(40);

        for (int i = 0; i < n; ++i) {
            s += pieces[pick(sizeof(pieces) / sizeof(pieces[0]))];

            if (chance(70)) {
                s += ' ';
            }
        }

        return s;
    }

    int pixelsOutsideTheGlass(const lgfx::LGFX_Sprite& s) {
        int n = 0;

        for (int y = 0; y < 240; ++y) {
            for (int x = 0; x < 240; ++x) {
                const float dx = static_cast<float>(x) + 0.5f - 120.0f;
                const float dy = static_cast<float>(y) + 0.5f - 120.0f;

                if (dx * dx + dy * dy > 121.0f * 121.0f && !ts::black(ts::pixel(s, x, y))) {
                    ++n;
                }
            }
        }

        return n;
    }
} // namespace

void setUp() {
}

void tearDown() {
}

void test_random_states_draw_quickly_and_inside_the_glass() {
    sim::Panel panel(40); // bands as on the ESP32
    RoundUi ui;
    ui.setBrand("DOMS COFFEE");
    MessageText text;
    std::string message;
    uint32_t now = 1000;
    double slowest = 0.0;

    for (int i = 0; i < 3000; ++i) {
        // The clock mostly runs forward, sometimes jumps or wraps around
        switch (pick(20)) {
            case 0:
                now += 0x80000000u;
                break;
            case 1:
                now = 0xFFFFFFFFu - static_cast<uint32_t>(pick(2000));
                break;
            case 2:
                now -= static_cast<uint32_t>(pick(5000)); // must not happen, must not hurt either
                break;
            default:
                now += static_cast<uint32_t>(pick(1500));
                break;
        }

        if (chance(8)) {
            message = randomText();
            splitMessage(message.c_str(), text);
            ui.showMessage(text.message());
        }
        else if (chance(8)) {
            ui.clearMessage();
        }

        if (chance(5)) {
            ui.play(static_cast<Animation>(pick(4)), now);
        }

        const Model model = randomModel();
        ui.update(model, now);
        ui.needsRedraw(now);

        const auto start = std::chrono::steady_clock::now();
        panel.render(ui, now);
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        slowest = std::max(slowest, ms);

        char where[64];
        snprintf(where, sizeof(where), "case %d took %.0f ms", i, ms);
        TEST_ASSERT_TRUE_MESSAGE(ms < 250.0, where); // a normal frame takes a few ms here, a runaway loop far longer

        snprintf(where, sizeof(where), "case %d draws outside the glass", i);
        const int outside = pixelsOutsideTheGlass(panel.screen);

        if (outside > 0) {
            // Picture and state for the report: test/fuzz-failure.png
            std::vector<uint8_t> rgb(240 * 240 * 3);

            for (int y = 0; y < 240; ++y) {
                for (int x = 0; x < 240; ++x) {
                    const Color c = ts::pixel(panel.screen, x, y);
                    rgb[(y * 240 + x) * 3] = c >> 16 & 0xFF;
                    rgb[(y * 240 + x) * 3 + 1] = c >> 8 & 0xFF;
                    rgb[(y * 240 + x) * 3 + 2] = c & 0xFF;
                }
            }

            png::write(RD_GOLDEN_DIR "/../fuzz-failure.png", 240, 240, rgb.data());
            printf("screen %s, mode %d, T %g, set %g, brew %g/%g s, weight %g/%g g, heater %g, message \"%s\"\n", screenName(ui.screen()), static_cast<int>(model.mode), model.temperature, model.setpoint, model.brewTime,
                   model.brewTargetTime, model.brewWeight, model.brewTargetWeight, model.heaterPercent, message.c_str());
        }

        TEST_ASSERT_EQUAL_INT_MESSAGE(0, outside, where);
    }

    printf("slowest frame %.1f ms\n", slowest);
}

void test_numbers_that_are_no_numbers() {
    char buf[16];

    for (const float v : {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity(), 1e30f, -3e9f}) {
        formatNumber(buf, sizeof(buf), v, 1, Language::German);
        TEST_ASSERT_EQUAL_STRING("--", buf);
    }

    formatNumber(buf, sizeof(buf), 9999.0f, 0, Language::German);
    TEST_ASSERT_EQUAL_STRING("9999", buf);
}

int main() {
    if (!RoundUi::begin()) {
        return 1;
    }

    UNITY_BEGIN();
    RUN_TEST(test_numbers_that_are_no_numbers);
    RUN_TEST(test_random_states_draw_quickly_and_inside_the_glass);
    return UNITY_END();
}
