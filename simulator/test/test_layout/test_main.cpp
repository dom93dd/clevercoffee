/**
 * @file test_main.cpp
 *
 * @brief Spacing rules for every screen in German and English:
 *
 * - content (texts, icons) keeps kFrameGap px away from the frame (ring, ticks, markers, status symbols)
 * - content keeps kGlassGap px away from the edge of the round glass
 * - texts above each other keep kTextGap px; lines of one paragraph (same text or hint font) kLineGap px
 * - the digits of the big number keep kBigGap px from its baseline; only the comma reaches into that gap
 *
 * Distances are measured on the rendered pixels (ink), not on font metrics. Frame and content come from
 * two renders with Painter::drawnLayers; each text is measured alone (Painter::textObserver).
 * RD_LAYOUT_BOXES=1 pio test -e test -f test_layout -v prints the ink box of every text.
 */

#include "../support/TestSupport.h"

#include "../../src/SimSupport.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace rd;

namespace {
    constexpr int kFrameGap = 8;
    constexpr int kGlassGap = 8;
    constexpr int kTextGap = 7;
    constexpr int kBigGap = 12;
    constexpr int kLineGap = 3;
    constexpr int kInk = 40;      // brightest channel above this counts as text or icon
    constexpr int kFrameInk = 20; // the dark ring track counts as frame

    struct TextBox {
            const rd::Font* font;
            std::string text;
            float x, y;
            Align align;
            int x0, y0, x1, y1; // ink bounds

            /** Bottom of the ink without descenders (the comma of the big number) */
            int baseline() const {
                return std::min(y1, static_cast<int>(std::floor(y)));
            }
    };

    std::vector<TextBox> gTexts;

    void observe(const rd::Font* font, const char* text, const float x, const float y, const Align align) {
        gTexts.push_back({font, text, x, y, align, 0, 0, -1, -1});
    }

    bool ink(const lgfx::LGFX_Sprite& s, const int x, const int y, const int threshold = kInk) {
        const Color c = ts::pixel(s, x, y);
        return std::max({ts::channel(c, 16), ts::channel(c, 8), ts::channel(c, 0)}) > threshold;
    }

    const char* fontName(const rd::Font* f) {
        if (f == rd::fonts::big()) return "big";
        if (f == rd::fonts::mid()) return "mid";
        if (f == rd::fonts::label()) return "label";
        if (f == rd::fonts::hint()) return "hint";
        if (f == rd::fonts::midSmall()) return "mid-small";
        if (f == rd::fonts::textSmall()) return "text-small";
        if (f == rd::fonts::textCompact()) return "text-compact";
        return "text";
    }

    /** Ink bounds of one text, drawn alone */
    void measure(TextBox& t, lgfx::LGFX_Sprite& scratch) {
        scratch.fillScreen(rgb(0, 0, 0));
        Painter p(scratch, 0);
        p.text(t.font, t.text.c_str(), t.x, t.y, rgb(255, 255, 255), t.align);
        t.x0 = t.y0 = 1000;
        t.x1 = t.y1 = -1;

        for (int y = 0; y < 240; ++y) {
            for (int x = 0; x < 240; ++x) {
                if (ink(scratch, x, y)) {
                    t.x0 = std::min(t.x0, x);
                    t.x1 = std::max(t.x1, x);
                    t.y0 = std::min(t.y0, y);
                    t.y1 = std::max(t.y1, y);
                }
            }
        }
    }

    const TextBox* textAt(const int x, const int y) {
        for (const auto& t : gTexts) {
            if (x >= t.x0 && x <= t.x1 && y >= t.y0 && y <= t.y1) {
                return &t;
            }
        }

        return nullptr;
    }

    bool animated(const char* name) {
        // Iris transitions draw their rim across the content on purpose
        return std::strcmp(name, "reveal") == 0 || std::strcmp(name, "close") == 0;
    }

    /** All rule violations of one screen, as readable lines */
    std::vector<std::string> audit(const sim::Scenario& sc, const Language lang) {
        std::vector<std::string> problems;
        sim::Panel frame(240);
        sim::Panel content(240);
        lgfx::LGFX_Sprite scratch;
        scratch.setColorDepth(16);
        scratch.createSprite(240, 240);

        Painter::drawnLayers = static_cast<uint8_t>(Layer::Frame);
        sim::renderScenario(sc, frame, lang);
        gTexts.clear();
        Painter::drawnLayers = static_cast<uint8_t>(Layer::Content);
        Painter::textObserver = observe;
        sim::renderScenario(sc, content, lang);
        Painter::textObserver = nullptr;
        Painter::drawnLayers = 0xFF;

        for (auto& t : gTexts) {
            measure(t, scratch);

            if (std::getenv("RD_LAYOUT_BOXES") != nullptr) {
                printf("BOX %s %s %-6s y=%5.1f ink x %d..%d y %d..%d \"%s\"\n", sc.name, lang == Language::German ? "de" : "en", fontName(t.font), t.y, t.x0, t.x1, t.y0, t.y1, t.text.c_str());
            }
        }

        const std::string where = std::string(sc.name) + " (" + (lang == Language::German ? "de" : "en") + "): ";
        char buf[256];

        // Content against frame and glass: report the closest spot once per screen
        int worstFrame = 1000;
        int fx = 0;
        int fy = 0;
        int worstGlass = 1000;
        int gx = 0;
        int gy = 0;

        for (int y = 0; y < 240; ++y) {
            for (int x = 0; x < 240; ++x) {
                if (!ink(content.screen, x, y)) {
                    continue;
                }

                const float r = std::hypot(static_cast<float>(x) + 0.5f - 120.0f, static_cast<float>(y) + 0.5f - 120.0f);
                const int glass = static_cast<int>(std::floor(120.0f - r));

                if (glass < worstGlass) {
                    worstGlass = glass;
                    gx = x;
                    gy = y;
                }

                if (animated(sc.name)) {
                    continue;
                }

                for (int dy = -kFrameGap; dy <= kFrameGap; ++dy) {
                    for (int dx = -kFrameGap; dx <= kFrameGap; ++dx) {
                        const int qx = x + dx;
                        const int qy = y + dy;

                        if (qx < 0 || qy < 0 || qx >= 240 || qy >= 240 || !ink(frame.screen, qx, qy, kFrameInk)) {
                            continue;
                        }

                        const int gap = static_cast<int>(std::floor(std::hypot(static_cast<float>(dx), static_cast<float>(dy)))) - 1;

                        if (gap < worstFrame) {
                            worstFrame = gap;
                            fx = x;
                            fy = y;
                        }
                    }
                }
            }
        }

        const auto what = [](const int x, const int y) {
            const TextBox* t = textAt(x, y);
            return t != nullptr ? "\"" + t->text + "\"" : std::string("icon");
        };

        if (worstFrame < kFrameGap) {
            snprintf(buf, sizeof(buf), "%s %d px from the ring/markers (min %d) at %d,%d", what(fx, fy).c_str(), worstFrame, kFrameGap, fx, fy);
            problems.push_back(where + buf);
        }

        if (worstGlass < kGlassGap) {
            snprintf(buf, sizeof(buf), "%s %d px from the glass edge (min %d) at %d,%d", what(gx, gy).c_str(), worstGlass, kGlassGap, gx, gy);
            problems.push_back(where + buf);
        }

        // Texts against each other
        for (size_t i = 0; i < gTexts.size(); ++i) {
            for (size_t j = i + 1; j < gTexts.size(); ++j) {
                const TextBox& a = gTexts[i];
                const TextBox& b = gTexts[j];

                if (a.x1 < 0 || b.x1 < 0 || a.x1 < b.x0 || b.x1 < a.x0) {
                    continue; // empty or side by side
                }

                const TextBox& upper = a.y0 <= b.y0 ? a : b;
                const TextBox& lower = a.y0 <= b.y0 ? b : a;
                const int gap = lower.y0 - upper.y1 - 1;
                const bool paragraph = upper.font == lower.font && upper.font != rd::fonts::label() && upper.font->size() <= 20;
                const int need = paragraph ? kLineGap : kTextGap;

                if (gap < need) {
                    snprintf(buf, sizeof(buf), "\"%s\" (%s) and \"%s\" (%s) %d px apart (min %d)", upper.text.c_str(), fontName(upper.font), lower.text.c_str(), fontName(lower.font), gap, need);
                    problems.push_back(where + buf);
                }
                else if (upper.font == rd::fonts::big() && lower.y0 - upper.baseline() - 1 < kBigGap) {
                    snprintf(buf, sizeof(buf), "\"%s\" (big) baseline and \"%s\" (%s) %d px apart (min %d)", upper.text.c_str(), lower.text.c_str(), fontName(lower.font), lower.y0 - upper.baseline() - 1, kBigGap);
                    problems.push_back(where + buf);
                }
                else if (lower.font == rd::fonts::big() && gap < kBigGap) {
                    snprintf(buf, sizeof(buf), "\"%s\" (%s) and \"%s\" (big) %d px apart (min %d)", upper.text.c_str(), fontName(upper.font), lower.text.c_str(), gap, kBigGap);
                    problems.push_back(where + buf);
                }
            }
        }

        return problems;
    }

    /** Texts of one scenario with their fonts and ink boxes */
    std::vector<TextBox> textsOf(const char* name, const Language lang) {
        const auto all = sim::scenarios();
        const sim::Scenario* sc = sim::findScenario(all, name);
        TEST_ASSERT_NOT_NULL_MESSAGE(sc, name);
        sim::Panel panel(240);
        lgfx::LGFX_Sprite scratch;
        scratch.setColorDepth(16);
        scratch.createSprite(240, 240);

        gTexts.clear();
        Painter::textObserver = observe;
        sim::renderScenario(*sc, panel, lang);
        Painter::textObserver = nullptr;

        for (auto& t : gTexts) {
            measure(t, scratch);
        }

        return gTexts;
    }

    const TextBox* find(const std::vector<TextBox>& texts, const char* start) {
        for (const auto& t : texts) {
            if (t.text.rfind(start, 0) == 0) {
                return &t;
            }
        }

        return nullptr;
    }
} // namespace

void setUp() {
}

void tearDown() {
}

void test_spacing_on_every_screen() {
    std::vector<std::string> problems;
    int screens = 0;

    for (const auto& sc : sim::scenarios()) {
        for (const Language lang : {Language::German, Language::English}) {
            for (const auto& p : audit(sc, lang)) {
                problems.push_back(p);
            }

            ++screens;
        }
    }

    for (const auto& p : problems) {
        printf("SPACING %s\n", p.c_str());
    }

    const std::string msg = std::to_string(problems.size()) + " spacing problems on " + std::to_string(screens) + " screens (see SPACING lines)";
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, static_cast<int>(problems.size()), msg.c_str());
}

// Feedback 01.10.2026 (screen numbers of review/round-display): font size per kind of line

void test_line_under_the_big_number_is_one_size_smaller() {
    // #18-#23, #26, #29, #34, #45, #46: "Ziel 25 s" / "Soll 94,0°" 19 px, the row below stays 20 px
    for (const Language lang : {Language::German, Language::English}) {
        const auto ready = textsOf("ready", lang);
        const TextBox* setpoint = find(ready, lang == Language::German ? "Soll " : "Set ");
        TEST_ASSERT_NOT_NULL(setpoint);
        TEST_ASSERT_TRUE(setpoint->font == rd::fonts::textSmall());
        TEST_ASSERT_TRUE(find(ready, lang == Language::German ? "25,3 s" : "25.3 s")->font == rd::fonts::text());

        const auto brew = textsOf("brew", lang);
        const TextBox* target = find(brew, lang == Language::German ? "Ziel " : "Target ");
        TEST_ASSERT_NOT_NULL(target);
        TEST_ASSERT_TRUE(target->font == rd::fonts::textSmall());
    }
}

void test_second_number_while_brewing_with_the_scale() {
    // #30-#33: "18,2 s" two sizes smaller than before (32 instead of 34 px) and further from the big number
    for (const Language lang : {Language::German, Language::English}) {
        const auto texts = textsOf("brew-scale", lang);
        const TextBox* weight = nullptr;
        const TextBox* time = find(texts, lang == Language::German ? "18,2 s" : "18.2 s");

        for (const auto& t : texts) {
            weight = t.font == rd::fonts::big() ? &t : weight;
        }

        TEST_ASSERT_NOT_NULL(weight);
        TEST_ASSERT_NOT_NULL(time);
        TEST_ASSERT_TRUE(time->font == rd::fonts::midSmall());
        TEST_ASSERT_GREATER_OR_EQUAL_INT(15, time->y0 - weight->baseline() - 1); // was 13
        TEST_ASSERT_GREATER_OR_EQUAL_INT(9, time->y0 - weight->y1 - 1);          // to the comma, was 7
        TEST_ASSERT_TRUE(find(texts, lang == Language::German ? "Ziel " : "Target ")->font == rd::fonts::textSmall());
    }
}

void test_long_message_line_uses_the_compact_font() {
    // #5: "4.0.3 + Rund-Display" came close to the ring, the line above did not
    const auto texts = textsOf("boot", Language::German);
    TEST_ASSERT_TRUE(find(texts, "4.0.3")->font == rd::fonts::textCompact());
    TEST_ASSERT_TRUE(find(texts, "CleverCoffee")->font == rd::fonts::text());
}

void test_long_messages_keep_their_line_breaks() {
    // #11-#13: one size smaller, same lines as before
    const char* expected[] = {"Kalibrierung läuft.", "Bitte in den nächsten", "10 Sekunden ein", "bekanntes Gewicht", "auflegen 267.00g"};
    const auto texts = textsOf("msg-calibrate-de", Language::German);
    TEST_ASSERT_EQUAL_INT(5, static_cast<int>(texts.size()));

    for (int i = 0; i < 5; ++i) {
        TEST_ASSERT_EQUAL_STRING(expected[i], texts[i].text.c_str());
        TEST_ASSERT_TRUE(texts[i].font == rd::fonts::textSmall());
    }
}

void test_dots_keep_the_rhythm_of_the_lines() {
    // #10: the gap above "...." was 17 px, the one below 10 px; now both look the same
    const auto texts = textsOf("msg-taring", Language::German);
    const TextBox* above = find(texts, "remove");
    const TextBox* dots = find(texts, "....");
    const TextBox* below = find(texts, "done");
    TEST_ASSERT_NOT_NULL(above);
    TEST_ASSERT_NOT_NULL(dots);
    TEST_ASSERT_NOT_NULL(below);
    TEST_ASSERT_INT_WITHIN(2, below->y0 - dots->y1, dots->y0 - above->y1);
}

int main() {
    if (!RoundUi::begin()) {
        return 1;
    }

    UNITY_BEGIN();
    RUN_TEST(test_spacing_on_every_screen);
    RUN_TEST(test_line_under_the_big_number_is_one_size_smaller);
    RUN_TEST(test_second_number_while_brewing_with_the_scale);
    RUN_TEST(test_long_message_line_uses_the_compact_font);
    RUN_TEST(test_long_messages_keep_their_line_breaks);
    RUN_TEST(test_dots_keep_the_rhythm_of_the_lines);
    return UNITY_END();
}
