/**
 * @file RoundDisplayPaint.cpp
 *
 * @brief Anti-aliased shapes via signed distances: every pixel near an edge gets
 *        the share of its area that lies inside the shape.
 */

#include "RoundDisplayPaint.h"

#include <algorithm>
#include <cmath>

namespace rd {

    namespace {
        constexpr float kDegToRad = 0.017453292519943295f;
        constexpr float kRadToDeg = 57.29577951308232f;

        float clamp01(const float v) {
            return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
        }

        // Share of a pixel that is covered when its center has the signed distance d to the edge
        float coverage(const float d) {
            return clamp01(0.5f - d);
        }

        float segmentDistance(const float x, const float y, const float x0, const float y0, const float x1, const float y1) {
            const float vx = x1 - x0;
            const float vy = y1 - y0;
            const float wx = x - x0;
            const float wy = y - y0;
            const float len2 = vx * vx + vy * vy;
            const float t = len2 > 0.0f ? clamp01((wx * vx + wy * vy) / len2) : 0.0f;
            const float dx = wx - vx * t;
            const float dy = wy - vy * t;
            return std::sqrt(dx * dx + dy * dy);
        }
    } // namespace

    Color mix(const Color a, const Color b, float t) {
        t = clamp01(t);
        Color out = 0;

        for (int shift = 0; shift <= 16; shift += 8) {
            const float ca = static_cast<float>(a >> shift & 0xFF);
            const float cb = static_cast<float>(b >> shift & 0xFF);
            out |= static_cast<Color>(ca + (cb - ca) * t + 0.5f) << shift;
        }

        return out;
    }

    Painter::Painter(lgfx::LGFX_Sprite& band, const int top) :
        band_(band), buffer_(static_cast<uint16_t*>(band.getBuffer())), top_(top), width_(band.width()), height_(band.height()) {
    }

    void Painter::clear(const Color c) {
        band_.fillScreen(c);
    }

    float Painter::px(const float cx, const float radius, const float angle) {
        return cx + radius * std::sin(angle * kDegToRad);
    }

    float Painter::py(const float cy, const float radius, const float angle) {
        return cy - radius * std::cos(angle * kDegToRad);
    }

    void Painter::blend(const int x, const int y, const Color c, const float alpha) {
        if (alpha > 0.0f) {
            blendFixed(x, y, c, static_cast<int>(alpha * 256.0f));
        }
    }

    void Painter::blendFixed(const int x, const int y, const Color c, const int a) {
        const int row = y - top_;

        if (a <= 0 || x < 0 || x >= width_ || row < 0 || row >= height_) {
            return;
        }

        // 16 bit sprites keep RGB565 with swapped bytes (the order the panel expects)
        uint16_t& pixel = buffer_[row * width_ + x];
        const uint16_t v = static_cast<uint16_t>(pixel >> 8 | pixel << 8);
        int r = v >> 11 & 0x1F;
        int g = v >> 5 & 0x3F;
        int b = v & 0x1F;
        r = r << 3 | r >> 2;
        g = g << 2 | g >> 4;
        b = b << 3 | b >> 2;

        r += (static_cast<int>(c >> 16 & 0xFF) - r) * a >> 8;
        g += (static_cast<int>(c >> 8 & 0xFF) - g) * a >> 8;
        b += (static_cast<int>(c & 0xFF) - b) * a >> 8;

        const auto out = static_cast<uint16_t>((r & 0xF8) << 8 | (g & 0xFC) << 3 | b >> 3);
        pixel = static_cast<uint16_t>(out >> 8 | out << 8);
    }

    template <typename Shade>
    void Painter::shadeArc(const float cx, const float cy, const float radius, const float width, const float a0, const float a1, const bool roundCaps, Shade shade) {
        const float sweep = a1 - a0;

        if (sweep < 0.0f || !drawing()) {
            return;
        }

        const float half = width * 0.5f;
        const float rOut = radius + half + 1.0f;
        const float rIn = std::max(0.0f, radius - half - 1.0f);
        const bool closed = sweep >= 360.0f;

        const float cap0x = px(cx, radius, a0);
        const float cap0y = py(cy, radius, a0);
        const float cap1x = px(cx, radius, a1);
        const float cap1y = py(cy, radius, a1);

        const int yStart = std::max(top_, static_cast<int>(std::floor(cy - rOut)));
        const int yEnd = std::min(top_ + height_ - 1, static_cast<int>(std::ceil(cy + rOut)));

        for (int y = yStart; y <= yEnd; ++y) {
            const float dy = static_cast<float>(y) + 0.5f - cy;
            const float outer2 = rOut * rOut - dy * dy;

            if (outer2 <= 0.0f) {
                continue;
            }

            const float xo = std::sqrt(outer2);
            const float inner2 = rIn * rIn - dy * dy;
            const float xi = inner2 > 0.0f ? std::sqrt(inner2) : 0.0f;

            // The ring crosses this row in two spans; they merge when the row misses the hole
            const int spans = xi > 0.0f ? 2 : 1;

            for (int s = 0; s < spans; ++s) {
                const float from = s == 0 ? cx - xo : cx + xi;
                const float to = spans == 1 ? cx + xo : (s == 0 ? cx - xi : cx + xo);
                const int xStart = std::max(0, static_cast<int>(std::floor(from)));
                const int xEnd = std::min(width_ - 1, static_cast<int>(std::ceil(to)));

                for (int x = xStart; x <= xEnd; ++x) {
                    const float dx = static_cast<float>(x) + 0.5f - cx;
                    const float r = std::sqrt(dx * dx + dy * dy);
                    const float radial = std::fabs(r - radius) - half;

                    // Caps sit inside the radial band, so nothing outside it can be covered
                    if (radial >= 0.5f) {
                        continue;
                    }

                    const float rel = std::fmod(std::atan2(dx, -dy) * kRadToDeg - a0 + 720.0f, 360.0f);
                    float d;
                    float t;

                    if (closed) {
                        d = radial;
                        t = rel / 360.0f;
                    }
                    else {
                        if (rel <= sweep) {
                            d = radial;
                            t = sweep > 0.0f ? rel / sweep : 0.0f;
                        }
                        else if (roundCaps) {
                            const float d0 = std::hypot(dx + cx - cap0x, dy + cy - cap0y);
                            const float d1 = std::hypot(dx + cx - cap1x, dy + cy - cap1y);
                            d = std::min(d0, d1) - half;
                            t = d0 < d1 ? 0.0f : 1.0f;
                        }
                        else {
                            const float past = std::min(rel - sweep, 360.0f - rel);
                            d = std::max(radial, r * std::sin(std::min(past, 90.0f) * kDegToRad));
                            t = 360.0f - rel < rel - sweep ? 0.0f : 1.0f;
                        }
                    }

                    const float a = coverage(d);

                    if (a > 0.0f) {
                        blend(x, y, shade(t), a);
                    }
                }
            }
        }
    }

    template <typename Distance>
    void Painter::fillShape(const float x0, const float y0, const float x1, const float y1, const Color c, Distance distance) {
        if (!drawing()) {
            return;
        }

        const int yStart = std::max(top_, static_cast<int>(std::floor(y0)) - 1);
        const int yEnd = std::min(top_ + height_ - 1, static_cast<int>(std::ceil(y1)) + 1);
        const int xStart = std::max(0, static_cast<int>(std::floor(x0)) - 1);
        const int xEnd = std::min(width_ - 1, static_cast<int>(std::ceil(x1)) + 1);

        for (int y = yStart; y <= yEnd; ++y) {
            for (int x = xStart; x <= xEnd; ++x) {
                const float a = coverage(distance(static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f));

                if (a > 0.0f) {
                    blend(x, y, c, a);
                }
            }
        }
    }

    void Painter::arc(const float cx, const float cy, const float radius, const float width, const float a0, const float a1, const Color c, const bool roundCaps) {
        shadeArc(cx, cy, radius, width, a0, a1, roundCaps, [c](float) { return c; });
    }

    void Painter::arcGradient(const float cx, const float cy, const float radius, const float width, const float a0, const float a1, const Color c0, const Color c1, const bool roundCaps) {
        shadeArc(cx, cy, radius, width, a0, a1, roundCaps, [c0, c1](const float t) { return mix(c0, c1, t); });
    }

    void Painter::circle(const float cx, const float cy, const float radius, const float width, const Color c) {
        arc(cx, cy, radius, width, 0.0f, 360.0f, c, false);
    }

    void Painter::disc(const float cx, const float cy, const float radius, const Color c) {
        if (!visible(cy - radius - 1.0f, cy + radius + 1.0f)) {
            return;
        }

        fillShape(cx - radius, cy - radius, cx + radius, cy + radius, c, [=](const float x, const float y) { return std::hypot(x - cx, y - cy) - radius; });
    }

    void Painter::line(const float x0, const float y0, const float x1, const float y1, const float width, const Color c) {
        const float half = width * 0.5f;

        if (!visible(std::min(y0, y1) - half - 1.0f, std::max(y0, y1) + half + 1.0f)) {
            return;
        }

        fillShape(std::min(x0, x1) - half, std::min(y0, y1) - half, std::max(x0, x1) + half, std::max(y0, y1) + half, c, [=](const float x, const float y) { return segmentDistance(x, y, x0, y0, x1, y1) - half; });
    }

    void Painter::tick(const float cx, const float cy, const float angle, const float r0, const float r1, const float width, const Color c) {
        line(px(cx, r0, angle), py(cy, r0, angle), px(cx, r1, angle), py(cy, r1, angle), width, c);
    }

    void Painter::mask(const float cx, const float cy, const float radius, const float feather) {
        if (!drawing()) {
            return;
        }

        for (int y = top_; y < top_ + height_; ++y) {
            const float dy = static_cast<float>(y) + 0.5f - cy;

            for (int x = 0; x < width_; ++x) {
                const float dx = static_cast<float>(x) + 0.5f - cx;
                const float d = std::sqrt(dx * dx + dy * dy) - radius;

                if (d > 0.0f) {
                    blend(x, y, 0, feather > 0.0f ? clamp01(d / feather) : 1.0f);
                }
            }
        }
    }

    void Painter::text(const Font* font, const char* s, const float x, const float y, const Color c, const Align align) {
        if (font == nullptr || s == nullptr || *s == '\0' || !drawing()) {
            return;
        }

        if (textObserver != nullptr) {
            textObserver(font, s, x, y, align);
        }

        const int baseline = static_cast<int>(std::lround(y));

        if (baseline + font->size() < top_ || baseline - 2 * font->size() >= top_ + height_) {
            return;
        }

        // Positions as LovyanGFX's drawString placed its VLW fonts
        int pen = static_cast<int>(std::lround(x)) + font->leftOverhang(s);

        if (align == Align::Center) {
            pen -= font->width(s) >> 1;
        }
        else if (align == Align::Right) {
            pen -= font->width(s);
        }

        Font::Glyph space;
        font->find(' ', space);
        Font::Glyph g;

        for (const char* p = s; *p != '\0';) {
            if (!font->find(Font::next(p), g)) {
                pen += space.advance;
                continue;
            }

            const int gx = pen + g.left;
            const int gy = baseline - g.top;
            const int row0 = std::max(0, top_ - gy);
            const int row1 = std::min<int>(g.height, top_ + height_ - gy);

            for (int row = row0; row < row1; ++row) {
                for (int col = 0; col < g.width; ++col) {
                    const int cov = g.coverage(col, row);

                    if (cov != 0) {
                        blendFixed(gx + col, gy + row, c, cov * 256 / 15);
                    }
                }
            }

            pen += g.advance;
        }
    }

    int Painter::textWidth(const Font* font, const char* s) {
        return font->width(s);
    }

} // namespace rd
