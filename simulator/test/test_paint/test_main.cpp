/**
 * @file test_main.cpp
 *
 * @brief Drawing primitives: anti-aliasing, angle convention, caps, clipping at band borders, mask, text.
 */

#include "../support/TestSupport.h"

#include <limits>

using rd::Painter;
using rd::rgb;

namespace {
    lgfx::LGFX_Sprite screen; // 240 x 240, one band covering the whole screen
    lgfx::LGFX_Sprite band;   // 240 x 40, a band somewhere on the screen

    constexpr rd::Color kWhite = rgb(255, 255, 255);
    constexpr rd::Color kRed = rgb(255, 0, 0);
    constexpr rd::Color kBlue = rgb(0, 0, 255);
} // namespace

void setUp() {
    screen.fillScreen(rgb(0, 0, 0));
    band.fillScreen(rgb(0, 0, 0));
}

void tearDown() {
}

void test_disc_center_gets_exact_color() {
    Painter p(screen, 0);
    p.disc(120, 120, 10, kRed);
    TEST_ASSERT_EQUAL_HEX32(kRed, ts::pixel(screen, 120, 120));
    TEST_ASSERT_EQUAL_HEX32(0, ts::pixel(screen, 120, 133)); // 13 px away: outside
}

void test_edge_pixels_are_blended() {
    // A 1 px wide line centred on x = 10.0 covers half of the pixels 9 and 10
    Painter p(screen, 0);
    p.line(10.0f, 50.0f, 10.0f, 90.0f, 1.0f, kWhite);
    const rd::Color c = ts::pixel(screen, 10, 70);
    TEST_ASSERT_INT_WITHIN(10, 128, ts::channel(c, 16));
    TEST_ASSERT_INT_WITHIN(10, 128, ts::channel(c, 8));
    TEST_ASSERT_INT_WITHIN(10, 128, ts::channel(c, 0));
    TEST_ASSERT_EQUAL_HEX32(0, ts::pixel(screen, 12, 70));
}

void test_angle_convention() {
    // 0 degrees is 12 o'clock, angles grow clockwise
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 120.0f, Painter::px(120, 100, 0));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 20.0f, Painter::py(120, 100, 0));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 220.0f, Painter::px(120, 100, 90));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 120.0f, Painter::py(120, 100, 90));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 220.0f, Painter::py(120, 100, 180));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 20.0f, Painter::px(120, 100, 270));
}

void test_arc_covers_its_span_only() {
    Painter p(screen, 0);
    p.arc(120, 120, 100, 10, 0, 90, kWhite);
    TEST_ASSERT_EQUAL_HEX32(kWhite, ts::at(screen, 100, 45));
    TEST_ASSERT_EQUAL_HEX32(kWhite, ts::at(screen, 103, 45)); // inside the 10 px width
    TEST_ASSERT_EQUAL_HEX32(0, ts::at(screen, 108, 45));      // outside the width
    TEST_ASSERT_EQUAL_HEX32(0, ts::at(screen, 100, 135));     // outside the span
    TEST_ASSERT_EQUAL_HEX32(0, ts::at(screen, 100, 300));
}

void test_round_and_flat_caps() {
    Painter p(screen, 0);
    p.arc(120, 120, 100, 10, 0, 90, kWhite, true);
    const rd::Color round = ts::at(screen, 100, 92.0f); // 3.5 px behind the end, inside the round cap
    setUp();
    p.arc(120, 120, 100, 10, 0, 90, kWhite, false);
    const rd::Color flat = ts::at(screen, 100, 92.0f);
    TEST_ASSERT_EQUAL_HEX32(kWhite, round);
    TEST_ASSERT_EQUAL_HEX32(0, flat);
}

void test_closed_ring_has_no_gap() {
    Painter p(screen, 0);
    p.arc(120, 120, 100, 10, 0, 360, kWhite);

    for (float a = 0; a < 360; a += 7.5f) {
        TEST_ASSERT_EQUAL_HEX32_MESSAGE(kWhite, ts::at(screen, 100, a), "gap in closed ring");
    }
}

void test_negative_sweep_draws_nothing() {
    Painter p(screen, 0);
    p.arc(120, 120, 100, 10, 90, 45, kWhite);
    TEST_ASSERT_EQUAL_INT(0, ts::countNonBlack(screen));
}

void test_gradient_runs_from_start_to_end_color() {
    Painter p(screen, 0);
    p.arcGradient(120, 120, 100, 10, 0, 180, kRed, kBlue);
    const rd::Color start = ts::at(screen, 100, 5);
    const rd::Color end = ts::at(screen, 100, 175);
    TEST_ASSERT_GREATER_THAN(ts::channel(start, 0), ts::channel(start, 16));
    TEST_ASSERT_GREATER_THAN(ts::channel(end, 16), ts::channel(end, 0));
}

void test_band_clips_everything_outside_its_rows() {
    // Band covers screen rows 40..79
    Painter p(band, 40);
    p.disc(120, 10, 8, kWhite);  // rows 2..18: completely above
    p.disc(120, 100, 8, kWhite); // rows 92..108: completely below
    p.arc(120, 120, 20, 4, 0, 360, kWhite);
    p.line(0, 0, 239, 30, 3, kWhite);
    TEST_ASSERT_EQUAL_INT(0, ts::countNonBlack(band));
}

void test_band_draws_the_part_inside_its_rows() {
    Painter p(band, 40);
    p.disc(120, 38, 6, kWhite);                               // rows 32..44, the lower part lies in the band
    TEST_ASSERT_EQUAL_HEX32(kWhite, ts::pixel(band, 120, 2)); // screen row 42
    TEST_ASSERT_EQUAL_HEX32(0, ts::pixel(band, 120, 10));     // screen row 50
}

void test_shapes_at_the_screen_edge_are_clipped() {
    Painter p(screen, 0);
    p.disc(-2, 120, 6, kWhite);
    p.disc(242, 120, 6, kWhite);
    p.arc(120, 120, 130, 20, 0, 360, kWhite); // mostly outside the sprite
    TEST_ASSERT_EQUAL_HEX32(kWhite, ts::pixel(screen, 0, 120));
    TEST_ASSERT_EQUAL_HEX32(kWhite, ts::pixel(screen, 239, 120));
}

void test_mask_darkens_outside_the_circle() {
    screen.fillScreen(kWhite);
    Painter p(screen, 0);
    p.mask(120, 120, 50, 0);
    TEST_ASSERT_EQUAL_HEX32(kWhite, ts::at(screen, 40, 0));
    TEST_ASSERT_EQUAL_HEX32(0, ts::at(screen, 60, 0));
}

void test_mask_feather_fades_out() {
    screen.fillScreen(kWhite);
    Painter p(screen, 0);
    p.mask(120, 120, 50, 10);
    const int middle = ts::channel(ts::at(screen, 55, 90), 16);
    TEST_ASSERT_INT_WITHIN(30, 128, middle);
    TEST_ASSERT_EQUAL_HEX32(0, ts::at(screen, 62, 90));
}

void test_mix_interpolates_and_clamps() {
    TEST_ASSERT_EQUAL_HEX32(0x808080, rd::mix(0x000000, 0xFFFFFF, 0.5f));
    TEST_ASSERT_EQUAL_HEX32(0x102030, rd::mix(0x102030, 0xFFFFFF, -1.0f));
    TEST_ASSERT_EQUAL_HEX32(0xFFFFFF, rd::mix(0x102030, 0xFFFFFF, 2.0f));
}

void test_text_is_drawn_only_in_its_band() {
    Painter inside(band, 0);
    inside.text(rd::fonts::label(), "BEREIT", 120, 30, kWhite);
    TEST_ASSERT_GREATER_THAN(50, ts::countNonBlack(band));

    band.fillScreen(rgb(0, 0, 0));
    Painter elsewhere(band, 100);
    elsewhere.text(rd::fonts::label(), "BEREIT", 120, 30, kWhite);
    TEST_ASSERT_EQUAL_INT(0, ts::countNonBlack(band));
}

void test_text_alignment() {
    Painter p(screen, 0);
    const int w = p.textWidth(rd::fonts::text(), "Soll");
    TEST_ASSERT_GREATER_THAN(10, w);

    p.text(rd::fonts::text(), "Soll", 100, 100, kWhite, rd::Align::Left);
    int minX = 240;

    for (int y = 80; y < 105; ++y) {
        for (int x = 0; x < 240; ++x) {
            if (!ts::black(ts::pixel(screen, x, y))) {
                minX = std::min(minX, x);
            }
        }
    }

    TEST_ASSERT_INT_WITHIN(3, 100, minX);
}

void test_font_decodes_utf8() {
    const char* s = "aÄ€\xF0\x9F\x98\x80";
    const char* p = s;
    TEST_ASSERT_EQUAL_UINT32('a', rd::Font::next(p));
    TEST_ASSERT_EQUAL_UINT32(0xC4, rd::Font::next(p));
    TEST_ASSERT_EQUAL_UINT32(0x20AC, rd::Font::next(p));
    TEST_ASSERT_EQUAL_UINT32(0x1F600, rd::Font::next(p));
    TEST_ASSERT_EQUAL_CHAR('\0', *p);

    const char* cyrillic = "\xD0\x96"; // U+0416, uses all five bits of the lead byte
    TEST_ASSERT_EQUAL_UINT32(0x416, rd::Font::next(cyrillic));

    const char* cut = "\xC3";          // sequence cut off at the end of the string
    TEST_ASSERT_EQUAL_UINT32(0xC3, rd::Font::next(cut));
    TEST_ASSERT_EQUAL_CHAR('\0', *cut);
}

void test_fonts_find_their_glyphs() {
    const rd::Font* all[] = {rd::fonts::big(), rd::fonts::mid(), rd::fonts::midSmall(), rd::fonts::text(), rd::fonts::textSmall(), rd::fonts::textCompact(), rd::fonts::hint(), rd::fonts::label()};
    const int sizes[] = {80, 34, 32, 20, 19, 18, 16, 19};
    rd::Font::Glyph g;

    for (size_t i = 0; i < sizeof(all) / sizeof(all[0]); ++i) {
        TEST_ASSERT_TRUE(all[i]->valid());
        TEST_ASSERT_EQUAL_INT(sizes[i], all[i]->size());
        TEST_ASSERT_TRUE(all[i]->find('0', g)); // digits are in every font
        TEST_ASSERT_TRUE(g.width > 0 && g.height > 0 && g.advance > 0);
        TEST_ASSERT_FALSE(all[i]->find(0x4E2D, g));
    }

    // First, last and an umlaut of the sorted table
    TEST_ASSERT_TRUE(rd::fonts::text()->find(' ', g));
    TEST_ASSERT_TRUE(rd::fonts::text()->find(0xC4, g));        // Ä
    TEST_ASSERT_TRUE(rd::fonts::textSmall()->find(0x2013, g)); // en dash, the highest code point
    TEST_ASSERT_TRUE(rd::fonts::textCompact()->find(0xD8, g)); // Ø

    // Second number while brewing with the scale: time and weight, also negative after taring
    TEST_ASSERT_EQUAL_STRING("", ts::missingGlyphs(rd::fonts::midSmall(), "-0123456789,. gs").c_str());
}

void test_font_sizes_get_smaller() {
    // The smaller text fonts exist to save a little room: same text, less width and height
    const char* s = "Soll 94,0°";
    const int w20 = rd::fonts::text()->width(s);
    const int w19 = rd::fonts::textSmall()->width(s);
    const int w18 = rd::fonts::textCompact()->width(s);
    TEST_ASSERT_TRUE(w20 > w19 && w19 > w18);
    TEST_ASSERT_TRUE(rd::fonts::text()->inkTop("S") > rd::fonts::textCompact()->inkTop("S"));
    TEST_ASSERT_TRUE(rd::fonts::mid()->inkTop("8") > rd::fonts::midSmall()->inkTop("8"));
}

void test_font_width_and_ink_top() {
    const rd::Font* f = rd::fonts::text();
    rd::Font::Glyph a;
    rd::Font::Glyph b;
    rd::Font::Glyph space;
    TEST_ASSERT_TRUE(f->find('S', a));
    TEST_ASSERT_TRUE(f->find('o', b));
    TEST_ASSERT_TRUE(f->find(' ', space));
    TEST_ASSERT_EQUAL_INT(a.advance + b.advance, f->width("So"));
    TEST_ASSERT_EQUAL_INT(a.advance + space.advance, f->width("S\xE4\xB8\xAD")); // missing glyph counts as a space

    // Dots sit far below the x-height, capitals above it
    TEST_ASSERT_TRUE(f->inkTop("....") < f->inkTop("x") / 2);
    TEST_ASSERT_TRUE(f->inkTop("Sx") > f->inkTop("x"));
    TEST_ASSERT_EQUAL_INT(0, f->inkTop("  "));
}

void test_text_blends_coverage() {
    // A glyph edge is a mix of text and background color, the stem the full text color
    Painter p(screen, 0);
    p.text(rd::fonts::big(), "1", 120, 150, rgb(255, 0, 0));
    int full = 0;
    int partial = 0;

    for (int y = 80; y < 160; ++y) {
        for (int x = 80; x < 160; ++x) {
            const int r = ts::channel(ts::pixel(screen, x, y), 16);
            full += r >= 247 ? 1 : 0;
            partial += r > 16 && r < 240 ? 1 : 0;
            TEST_ASSERT_EQUAL_INT(0, ts::channel(ts::pixel(screen, x, y), 8)); // never another color
        }
    }

    TEST_ASSERT_GREATER_THAN(300, full);
    TEST_ASSERT_GREATER_THAN(20, partial);
}

void test_broken_coordinates_draw_nothing() {
    // NaN, infinity and values beyond int must neither draw garbage nor run into undefined behaviour
    // (pio test -e sanitize reports the latter); huge loops from such bounds would hang the ESP32
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    Painter p(screen, 0);

    for (const float bad : {nan, inf, -inf}) {
        p.arc(bad, 120, 100, 8, 0, 90, kWhite);
        p.arc(120, 120, 100, 8, 0, bad, kWhite);
        p.arcGradient(120, bad, 100, 8, 0, 90, kWhite, kWhite);
        p.circle(120, 120, bad, 8, kWhite);
        p.disc(bad, bad, 10, kWhite);
        p.line(bad, 10, 100, 100, 2, kWhite);
        p.tick(120, 120, bad, 90, 100, 2, kWhite);
        p.text(rd::fonts::text(), "Soll", bad, 100, kWhite);
        p.text(rd::fonts::text(), "Soll", 100, bad, kWhite);
        p.mask(120, 120, bad, 1);
    }

    // Finite but beyond int: the shapes lie far outside the screen
    for (const float far : {3e9f, -3e9f, 1e30f, -1e30f}) {
        p.arc(far, 120, 100, 8, 0, 90, kWhite);
        p.arcGradient(120, far, 100, 8, 0, 90, kWhite, kWhite);
        p.disc(far, far, 10, kWhite);
        p.line(far, far, far + 10.0f, far, 2, kWhite);
        p.text(rd::fonts::text(), "Soll", far, 100, kWhite);
        p.text(rd::fonts::text(), "Soll", 100, far, kWhite);
    }

    TEST_ASSERT_EQUAL_INT(0, ts::countNonBlack(screen));
}

int main() {
    if (!rd::RoundUi::begin()) {
        return 1; // fonts could not be loaded
    }

    screen.setColorDepth(16);
    screen.createSprite(240, 240);
    band.setColorDepth(16);
    band.createSprite(240, 40);

    UNITY_BEGIN();
    RUN_TEST(test_disc_center_gets_exact_color);
    RUN_TEST(test_edge_pixels_are_blended);
    RUN_TEST(test_angle_convention);
    RUN_TEST(test_arc_covers_its_span_only);
    RUN_TEST(test_round_and_flat_caps);
    RUN_TEST(test_closed_ring_has_no_gap);
    RUN_TEST(test_negative_sweep_draws_nothing);
    RUN_TEST(test_gradient_runs_from_start_to_end_color);
    RUN_TEST(test_band_clips_everything_outside_its_rows);
    RUN_TEST(test_band_draws_the_part_inside_its_rows);
    RUN_TEST(test_shapes_at_the_screen_edge_are_clipped);
    RUN_TEST(test_mask_darkens_outside_the_circle);
    RUN_TEST(test_mask_feather_fades_out);
    RUN_TEST(test_mix_interpolates_and_clamps);
    RUN_TEST(test_text_is_drawn_only_in_its_band);
    RUN_TEST(test_text_alignment);
    RUN_TEST(test_font_decodes_utf8);
    RUN_TEST(test_fonts_find_their_glyphs);
    RUN_TEST(test_font_sizes_get_smaller);
    RUN_TEST(test_font_width_and_ink_top);
    RUN_TEST(test_text_blends_coverage);
    RUN_TEST(test_broken_coordinates_draw_nothing);
    return UNITY_END();
}
