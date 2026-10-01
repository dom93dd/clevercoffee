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
        const std::string missing = "missing golden image " + file + " (run the tests once with RD_UPDATE_GOLDEN=1)";
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
    return UNITY_END();
}
