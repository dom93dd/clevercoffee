/**
 * @file TestSupport.h
 *
 * @brief Helpers shared by the test suites of the round display UI.
 */

#pragma once

#include <LovyanGFX.hpp>
#include <RoundDisplayFonts.h>
#include <RoundDisplayPaint.h>
#include <RoundDisplayUi.h>
#include <unity.h>

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <string>

namespace ts {

    /** Pixel of a 16 bit sprite as 0xRRGGBB (RGB565 expanded like the simulator does) */
    inline rd::Color pixel(const lgfx::LGFX_Sprite& s, const int x, const int y) {
        const uint16_t raw = static_cast<const uint16_t*>(s.getBuffer())[y * s.width() + x];
        const uint16_t v = static_cast<uint16_t>(raw >> 8 | raw << 8);
        const uint32_t r = v >> 11 & 0x1F;
        const uint32_t g = v >> 5 & 0x3F;
        const uint32_t b = v & 0x1F;
        return (r << 3 | r >> 2) << 16 | (g << 2 | g >> 4) << 8 | (b << 3 | b >> 2);
    }

    inline int channel(const rd::Color c, const int shift) {
        return static_cast<int>(c >> shift & 0xFF);
    }

    /** Largest difference of the three color channels */
    inline int difference(const rd::Color a, const rd::Color b) {
        int d = 0;

        for (int shift = 0; shift <= 16; shift += 8) {
            d = std::max(d, std::abs(channel(a, shift) - channel(b, shift)));
        }

        return d;
    }

    inline bool black(const rd::Color c) {
        return c == 0;
    }

    /** Pixel of a full screen sprite on a circle around the screen center, angle as in Painter */
    inline rd::Color at(const lgfx::LGFX_Sprite& s, const float radius, const float angle, const float cx = 120.0f, const float cy = 120.0f) {
        const float x = rd::Painter::px(cx, radius, angle);
        const float y = rd::Painter::py(cy, radius, angle);
        return pixel(s, static_cast<int>(std::floor(x)), static_cast<int>(std::floor(y)));
    }

    inline int countNonBlack(const lgfx::LGFX_Sprite& s) {
        int n = 0;

        for (int y = 0; y < s.height(); ++y) {
            for (int x = 0; x < s.width(); ++x) {
                n += black(pixel(s, x, y)) ? 0 : 1;
            }
        }

        return n;
    }

    /** Next code point of a UTF-8 string, advancing p */
    inline uint32_t nextCodePoint(const char*& p) {
        const auto c = static_cast<uint8_t>(*p++);

        if (c < 0x80) {
            return c;
        }

        int extra = c >= 0xF0 ? 3 : (c >= 0xE0 ? 2 : 1);
        uint32_t cp = c & (0x3F >> extra);

        while (extra-- > 0 && *p != '\0') {
            cp = cp << 6 | (static_cast<uint8_t>(*p++) & 0x3F);
        }

        return cp;
    }

    /** Characters of text that the font does not contain (empty if all are there) */
    inline std::string missingGlyphs(const rd::Font* font, const char* text) {
        std::string missing;
        rd::Font::Glyph glyph;

        for (const char* p = text; *p != '\0';) {
            const char* start = p;
            const uint32_t cp = nextCodePoint(p);

            if (cp != ' ' && !font->find(cp, glyph)) {
                missing.append(start, p);
            }
        }

        return missing;
    }

    inline bool hasLowercase(const char* text) {
        for (const char* p = text; *p != '\0'; ++p) {
            if (*p >= 'a' && *p <= 'z') {
                return true;
            }
        }

        return false;
    }

} // namespace ts
