/**
 * @file test_main.cpp
 *
 * @brief Whole screens: every scenario of the simulator in German and English.
 *
 * - drawing in bands gives exactly the same picture as drawing the whole screen at once
 * - nothing is drawn outside the round glass
 * - every screen shows something
 * - the picture matches the golden image in test/golden (RD_UPDATE_GOLDEN=1 rewrites them)
 * - the fonts contain every character of every text, and every text fits into the circle
 */

#include "../support/TestSupport.h"

#include "../../src/Png.h"
#include "../../src/SimSupport.h"

#include <RoundDisplayControl.h>
#include <RoundDisplayStrings.h>

#include <sys/stat.h>

#include <array>

#include <cstring>
#include <string>
#include <vector>

using namespace rd;

namespace {
    const std::vector<sim::Scenario>& all() {
        static const std::vector<sim::Scenario> list = sim::scenarios();
        return list;
    }

    const Language kLanguages[] = {Language::German, Language::English};

    const char* languageName(const Language lang) {
        return lang == Language::German ? "de" : "en";
    }

    std::string where(const sim::Scenario& sc, const Language lang) {
        return std::string(sc.name) + " (" + languageName(lang) + ")";
    }

    std::vector<uint8_t> toRgb(const lgfx::LGFX_Sprite& s) {
        std::vector<uint8_t> rgb(static_cast<size_t>(s.width()) * s.height() * 3);

        for (int y = 0; y < s.height(); ++y) {
            for (int x = 0; x < s.width(); ++x) {
                const Color c = ts::pixel(s, x, y);
                uint8_t* p = &rgb[(static_cast<size_t>(y) * s.width() + x) * 3];
                p[0] = c >> 16 & 0xFF;
                p[1] = c >> 8 & 0xFF;
                p[2] = c & 0xFF;
            }
        }

        return rgb;
    }
} // namespace

void setUp() {
}

void tearDown() {
}

void test_bands_give_the_same_picture_as_one_frame() {
    // 40 is the firmware's band height; 24 and 50 (does not divide 240) try other borders
    sim::Panel whole(240);
    sim::Panel bands40(40);
    sim::Panel bands24(24);
    sim::Panel bands50(50);
    const size_t bytes = 240 * 240 * 2;

    for (const auto& sc : all()) {
        for (const Language lang : kLanguages) {
            sim::renderScenario(sc, whole, lang);

            for (sim::Panel* banded : {&bands40, &bands24, &bands50}) {
                sim::renderScenario(sc, *banded, lang);
                const std::string msg = where(sc, lang) + ", band height " + std::to_string(banded->bands[0].height());
                TEST_ASSERT_EQUAL_MEMORY_MESSAGE(whole.screen.getBuffer(), banded->screen.getBuffer(), bytes, msg.c_str());
            }
        }
    }
}

void test_nothing_is_drawn_outside_the_glass() {
    sim::Panel panel;

    for (const auto& sc : all()) {
        for (const Language lang : kLanguages) {
            sim::renderScenario(sc, panel, lang);

            for (int y = 0; y < 240; ++y) {
                for (int x = 0; x < 240; ++x) {
                    const float dx = static_cast<float>(x) + 0.5f - 120.0f;
                    const float dy = static_cast<float>(y) + 0.5f - 120.0f;

                    if (dx * dx + dy * dy > 120.0f * 120.0f && !ts::black(ts::pixel(panel.screen, x, y))) {
                        const std::string msg = where(sc, lang) + ": pixel " + std::to_string(x) + "," + std::to_string(y) + " outside the circle";
                        TEST_FAIL_MESSAGE(msg.c_str());
                    }
                }
            }
        }
    }
}

void test_every_screen_shows_something() {
    sim::Panel panel;

    for (const auto& sc : all()) {
        for (const Language lang : kLanguages) {
            sim::renderScenario(sc, panel, lang);
            const int lit = ts::countNonBlack(panel.screen);
            const int minimum = std::strcmp(sc.name, "close") == 0 ? 20 : 400; // the iris is nearly shut there
            const std::string msg = where(sc, lang) + " is (almost) empty";
            TEST_ASSERT_GREATER_OR_EQUAL_MESSAGE(minimum, lit, msg.c_str());
        }
    }
}

void test_screens_match_the_golden_images() {
    const bool update = getenv("RD_UPDATE_GOLDEN") != nullptr;
    const std::string dir = RD_GOLDEN_DIR;
    const std::string failedDir = dir + "/failed";
    sim::Panel panel;
    int compared = 0;

    for (const auto& sc : all()) {
        sim::renderScenario(sc, panel, Language::German);
        const std::vector<uint8_t> actual = toRgb(panel.screen);
        const std::string file = dir + "/" + sc.name + ".png";

        if (update) {
            mkdir(dir.c_str(), 0755);
            TEST_ASSERT_TRUE_MESSAGE(png::write(file.c_str(), 240, 240, actual.data()), file.c_str());
            continue;
        }

        int w = 0;
        int h = 0;
        std::vector<uint8_t> expected;
        const std::string missing = "missing golden image " + file + " (run once: RD_UPDATE_GOLDEN=1 pio test -e test -f test_screens)";
        TEST_ASSERT_TRUE_MESSAGE(png::read(file.c_str(), w, h, expected), missing.c_str());
        TEST_ASSERT_EQUAL_INT(240, w);
        TEST_ASSERT_EQUAL_INT(240, h);

        // Tiny differences may come from another compiler or libm; real changes touch many pixels
        int different = 0;

        for (size_t i = 0; i < actual.size(); i += 3) {
            int d = 0;

            for (int c = 0; c < 3; ++c) {
                d = std::max(d, std::abs(static_cast<int>(actual[i + c]) - static_cast<int>(expected[i + c])));
            }

            different += d > 24 ? 1 : 0;
        }

        if (different > 115) {
            mkdir(failedDir.c_str(), 0755);
            const std::string out = failedDir + "/" + sc.name + ".png";
            png::write(out.c_str(), 240, 240, actual.data());
            const std::string msg = std::string(sc.name) + ": " + std::to_string(different) + " pixels differ from the golden image, see " + out;
            TEST_FAIL_MESSAGE(msg.c_str());
        }

        ++compared;
    }

    if (!update) {
        TEST_ASSERT_EQUAL_INT(static_cast<int>(all().size()), compared);
    }
}

void test_fonts_contain_every_ui_character() {
    for (const Strings* table : {&kGerman, &kEnglish}) {
        for (size_t i = 0; i < kStringCount; ++i) {
            const char* text = stringAt(*table, i);
            std::string missing = ts::missingGlyphs(rd::fonts::text(), text);
            std::string msg = std::string("text font lacks '") + missing + "' for \"" + text + "\"";
            TEST_ASSERT_TRUE_MESSAGE(missing.empty(), msg.c_str());

            if (!ts::hasLowercase(text)) {
                missing = ts::missingGlyphs(rd::fonts::label(), text);
                msg = std::string("label font lacks '") + missing + "' for \"" + text + "\"";
                TEST_ASSERT_TRUE_MESSAGE(missing.empty(), msg.c_str());
            }
        }
    }
}

void test_fonts_contain_numbers_units_and_names() {
    const char* bigTexts[] = {"0123456789", "-49,9", "93.4", "3/5"};
    const char* midTexts[] = {"-49,9°", "71°", "21,6 g", "18,2 s", "93.4°"};
    const char* labelTexts[] = {sim::kDefaultBrand, "CLEVERCOFFEE", "CAFFÈ DOMINIK", "DOM'S COFFEE", "ORIONE · DOMINIK"};

    for (const char* t : bigTexts) {
        TEST_ASSERT_EQUAL_STRING_MESSAGE("", ts::missingGlyphs(rd::fonts::big(), t).c_str(), t);
    }

    for (const char* t : midTexts) {
        TEST_ASSERT_EQUAL_STRING_MESSAGE("", ts::missingGlyphs(rd::fonts::mid(), t).c_str(), t);
    }

    for (const char* t : labelTexts) {
        TEST_ASSERT_EQUAL_STRING_MESSAGE("", ts::missingGlyphs(rd::fonts::label(), t).c_str(), t);
    }
}

void test_fonts_contain_the_firmware_message_titles() {
    // Titles the firmware sends (languages.h in German, English, Spanish, and literal texts)
    const char* titles[] = {
        "Verbinde WLAN:",
        "IP-Adresse:",
        "Kein ",
        "Offline AP starten",
        "Portal AP starten",
        "Kalibrierung startet",
        "Kalibrierung abgeschlossen!",
        "Connecting to WiFi:",
        "IP Address:",
        "No ",
        "Starting Offline AP",
        "Starting Portal AP",
        "Calibration coming up",
        "Calibration done!",
        "Wifi conectado:",
        "Dirección IP:",
        "Iniciando AP offline",
        "Iniciando Portal AP",
        "Calibración iniciando",
        "Calibración completa!",
        "Version ",
        "REBOOTING",
        "Bluetooth scales",
        "Taring scale,",
        "Scale",
    };

    for (const char* t : titles) {
        // Short titles become capitals (label font), long ones keep their case (text font)
        MessageText m;
        splitMessage(t, m);
        const bool label = !ts::hasLowercase(m.lines[0]);
        const std::string missing = ts::missingGlyphs(label ? rd::fonts::label() : rd::fonts::text(), m.lines[0]);
        const std::string msg = std::string(label ? "label" : "text") + " font lacks '" + missing + "' for the title \"" + m.lines[0] + "\"";
        TEST_ASSERT_TRUE_MESSAGE(missing.empty(), msg.c_str());
    }
}

void test_texts_fit_into_the_circle() {
    // Widest rows of the layout: labels at y 72 (190 px inside the ring), text rows at y 180 (176 px),
    // the "no WiFi" hint with its icon at y 205 (126 px)
    lgfx::LGFX_Sprite scratch;
    scratch.setColorDepth(16);
    scratch.createSprite(240, 8);
    Painter p(scratch, 0);

    for (const Strings* table : {&kGerman, &kEnglish}) {
        for (size_t i = 0; i < kStringCount; ++i) {
            const char* text = stringAt(*table, i);
            const bool label = !ts::hasLowercase(text);
            const int width = p.textWidth(label ? rd::fonts::label() : rd::fonts::text(), text);
            const std::string msg = std::string("\"") + text + "\" is " + std::to_string(width) + " px wide";
            TEST_ASSERT_LESS_OR_EQUAL_MESSAGE(label ? 190 : 176, width, msg.c_str());
        }

        const int hint = p.textWidth(rd::fonts::text(), table->noWifi) + 26;
        TEST_ASSERT_LESS_OR_EQUAL_MESSAGE(126, hint, table->noWifi);
    }
}

void test_status_symbols_are_mirrored() {
    // Heater symbol (left) and scale symbol (right) in the opening of the gauge: same height, mirrored
    const sim::Scenario* sc = sim::findScenario(all(), "scale-ok");
    TEST_ASSERT_NOT_NULL(sc);
    sim::Panel panel;
    sim::renderScenario(*sc, panel, Language::German);

    int minX[2] = {240, 240}, maxX[2] = {0, 0}, minY[2] = {240, 240}, maxY[2] = {0, 0};

    const auto centroid = [&](const int x0, const int x1, float& cx, float& cy) {
        float sum = 0;
        cx = cy = 0;
        const int k = x0 < 120 ? 0 : 1;

        for (int y = 204; y <= 226; ++y) {
            for (int x = x0; x <= x1; ++x) {
                const Color c = ts::pixel(panel.screen, x, y);
                const float w = static_cast<float>(std::max({ts::channel(c, 16), ts::channel(c, 8), ts::channel(c, 0)}));

                if (w > 30.0f) {
                    sum += w;
                    cx += w * (static_cast<float>(x) + 0.5f);
                    cy += w * (static_cast<float>(y) + 0.5f);
                    minX[k] = std::min(minX[k], x);
                    maxX[k] = std::max(maxX[k], x);
                    minY[k] = std::min(minY[k], y);
                    maxY[k] = std::max(maxY[k], y);
                }
            }
        }

        TEST_ASSERT_GREATER_THAN_MESSAGE(0, static_cast<int>(sum), "symbol missing");
        cx /= sum;
        cy /= sum;
    };

    float lx, ly, rx, ry;
    centroid(44, 78, lx, ly);
    centroid(162, 196, rx, ry);
    const std::string msg = "heater at " + std::to_string(lx) + "," + std::to_string(ly) + ", scale at " + std::to_string(rx) + "," + std::to_string(ry);
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(1.0f, ly, ry, msg.c_str());
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(1.5f, 240.0f - lx, rx, msg.c_str());

    // Same size: width and height of both symbols within a pixel
    const std::string size = "heater " + std::to_string(maxX[0] - minX[0] + 1) + "x" + std::to_string(maxY[0] - minY[0] + 1) + ", scale " + std::to_string(maxX[1] - minX[1] + 1) + "x" + std::to_string(maxY[1] - minY[1] + 1);
    TEST_ASSERT_INT_WITHIN_MESSAGE(1, maxX[0] - minX[0], maxX[1] - minX[1], size.c_str());
    TEST_ASSERT_INT_WITHIN_MESSAGE(1, maxY[0] - minY[0], maxY[1] - minY[1], size.c_str());
    TEST_ASSERT_INT_WITHIN_MESSAGE(1, minY[0], minY[1], size.c_str());
}

void test_progress_rings_start_at_the_zero_mark() {
    // The fill starts exactly at 12 o'clock: 2.5 px before it the ring is still empty, 2.5 px after it filled
    // (a round cap used to stick out in front of the zero mark)
    const Color track = rgb(34, 34, 36);

    for (const char* name : {"brew", "scale-18", "flush"}) {
        const sim::Scenario* sc = sim::findScenario(all(), name);
        TEST_ASSERT_NOT_NULL(sc);
        sim::Panel panel;
        sim::renderScenario(*sc, panel, Language::German);
        const Color before = ts::pixel(panel.screen, 117, 9);
        const Color after = ts::pixel(panel.screen, 122, 9);
        TEST_ASSERT_LESS_OR_EQUAL_MESSAGE(12, ts::difference(before, track), (std::string(name) + ": ring filled before the zero mark").c_str());
        TEST_ASSERT_GREATER_THAN_MESSAGE(40, ts::difference(after, track), (std::string(name) + ": ring not filled after the zero mark").c_str());
    }
}

void test_hint_font_contains_every_hint() {
    // Texts drawn with the smaller hint font, plus the digits and degree sign of "bis unter 99°"
    for (const Strings* t : {&kGerman, &kEnglish}) {
        for (const char* text : {t->heaterOff, t->heaterOffUntil, t->checkSensor, t->refill, t->pidOffHint, t->noWifi, t->scaleFault, t->scaleDisconnected, "0123456789°"}) {
            const std::string missing = ts::missingGlyphs(rd::fonts::hint(), text);
            TEST_ASSERT_TRUE_MESSAGE(missing.empty(), (std::string("hint font lacks '") + missing + "' for \"" + text + "\"").c_str());
        }
    }
}

void test_scale_symbol_always_shows_its_state() {
    // Right of the heater bar: green connected, red fault, grey not connected, fainter grey and crossed out without a scale
    struct Look {
            int r, g, b, lit;
    };

    const auto look = [](const char* name) {
        const sim::Scenario* sc = sim::findScenario(all(), name);
        TEST_ASSERT_NOT_NULL(sc);
        sim::Panel panel;
        sim::renderScenario(*sc, panel, Language::German);
        Look l{0, 0, 0, 0};

        for (int y = 204; y <= 226; ++y) {
            for (int x = 162; x <= 196; ++x) {
                const Color c = ts::pixel(panel.screen, x, y);

                if (std::max({ts::channel(c, 16), ts::channel(c, 8), ts::channel(c, 0)}) > 60) {
                    l.r += ts::channel(c, 16);
                    l.g += ts::channel(c, 8);
                    l.b += ts::channel(c, 0);
                    ++l.lit;
                }
            }
        }

        TEST_ASSERT_GREATER_THAN_MESSAGE(15, l.lit, (std::string(name) + ": no scale symbol").c_str());
        l.r /= l.lit;
        l.g /= l.lit;
        l.b /= l.lit;
        return l;
    };

    const Look ok = look("scale-ok");
    TEST_ASSERT_GREATER_THAN_MESSAGE(ok.r + 40, ok.g, "a connected scale is green");

    const Look fault = look("scale-fault");
    TEST_ASSERT_GREATER_THAN_MESSAGE(fault.g + 40, fault.r, "a scale fault is red");

    const Look waiting = look("scale-lost"); // switched on, not connected
    const Look none = look("ready");         // no scale
    TEST_ASSERT_INT_WITHIN_MESSAGE(12, waiting.r, waiting.g, "a scale that is not connected is grey");
    TEST_ASSERT_INT_WITHIN_MESSAGE(12, none.r, none.g, "without a scale the symbol is grey");
    TEST_ASSERT_GREATER_THAN_MESSAGE(none.g + 25, waiting.g, "not connected is brighter than no scale");

    // The cross runs through the inside of the scale's base, where the symbol itself has no lines
    const auto crossPixel = [](const char* name) {
        sim::Panel panel;
        sim::renderScenario(*sim::findScenario(all(), name), panel, Language::German);
        const float x = Painter::px(120.0f, 111.0f, 147.5f) - 3.2f; // 30 % along the cross line
        const float y = Painter::py(120.0f, 111.0f, 147.5f) + 1.84f;
        const Color c = ts::pixel(panel.screen, static_cast<int>(x), static_cast<int>(y));
        return std::max({ts::channel(c, 16), ts::channel(c, 8), ts::channel(c, 0)});
    };

    TEST_ASSERT_GREATER_THAN_MESSAGE(40, crossPixel("ready"), "no scale: the symbol is crossed out");
    TEST_ASSERT_LESS_THAN_MESSAGE(15, crossPixel("scale-lost"), "not connected: the symbol is not crossed out");
}

int main() {
    if (!RoundUi::begin()) {
        return 1;
    }

    UNITY_BEGIN();
    RUN_TEST(test_bands_give_the_same_picture_as_one_frame);
    RUN_TEST(test_nothing_is_drawn_outside_the_glass);
    RUN_TEST(test_every_screen_shows_something);
    RUN_TEST(test_screens_match_the_golden_images);
    RUN_TEST(test_fonts_contain_every_ui_character);
    RUN_TEST(test_fonts_contain_numbers_units_and_names);
    RUN_TEST(test_fonts_contain_the_firmware_message_titles);
    RUN_TEST(test_texts_fit_into_the_circle);
    RUN_TEST(test_status_symbols_are_mirrored);
    RUN_TEST(test_progress_rings_start_at_the_zero_mark);
    RUN_TEST(test_hint_font_contains_every_hint);
    RUN_TEST(test_scale_symbol_always_shows_its_state);
    return UNITY_END();
}
