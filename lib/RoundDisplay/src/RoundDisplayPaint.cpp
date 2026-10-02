/**
 * @file RoundDisplayPaint.cpp
 *
 * @brief Anti-aliased shapes via signed distances: every pixel near an edge gets
 *        the share of its area that lies inside the shape.
 */

#include "RoundDisplayPaint.h"

#include <algorithm>
#include <cmath>
#include <initializer_list>

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

        bool finite(const std::initializer_list<float> values) {
            for (const float v : values) {
                if (!std::isfinite(v)) {
                    return false;
                }
            }

            return true;
        }

        // Pixel index of a coordinate; huge values are clamped first, their conversion to int would be undefined
        int pixelFloor(const float v) {
            return static_cast<int>(std::floor(std::max(-16384.0f, std::min(16384.0f, v))));
        }

        int pixelCeil(const float v) {
            return static_cast<int>(std::ceil(std::max(-16384.0f, std::min(16384.0f, v))));
        }

        /** atan2 in degrees, max. error about 0.001 degrees; much cheaper than std::atan2 on the ESP32 */
        float fastAtan2Deg(const float y, const float x) {
            const float ax = std::fabs(x);
            const float ay = std::fabs(y);
            const float big = std::max(ax, ay);

            if (big == 0.0f) {
                return 0.0f;
            }

            const float a = std::min(ax, ay) / big;
            const float s = a * a;
            float r = ((-0.0464964749f * s + 0.15931422f) * s - 0.327622764f) * s * a + a;

            if (ay > ax) {
                r = 1.57079637f - r;
            }

            if (x < 0.0f) {
                r = 3.14159274f - r;
            }

            return (y < 0.0f ? -r : r) * kRadToDeg;
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

    template <bool Gradient, typename Shade>
    void Painter::shadeArc(const float cx, const float cy, const float radius, const float width, const float a0, const float a1, const bool roundCaps, Shade shade) {
        const float sweep = a1 - a0;

        // A broken value (NaN, infinity) from the machine draws nothing instead of garbage
        if (!finite({cx, cy, radius, width, a0, a1}) || sweep < 0.0f || !drawing()) {
            return;
        }

        // Per pixel this avoids atan2 and fmod (on the ESP32 about 190 and 100 instructions): whether a
        // pixel lies within the sweep follows from its angle to the middle of the arc, a dot product.
        const float half = width * 0.5f;
        const float rOut = radius + half + 1.0f;
        const float rIn = std::max(0.0f, radius - half - 1.0f);
        const bool closed = sweep >= 360.0f;
        const float start0 = a0 - 360.0f * std::floor(a0 / 360.0f); // 0..360
        const float sx = std::sin(a0 * kDegToRad);
        const float sy = -std::cos(a0 * kDegToRad);
        const float ex = std::sin(a1 * kDegToRad);
        const float ey = -std::cos(a1 * kDegToRad);
        const float mid = (a0 + a1) * 0.5f * kDegToRad;
        const float mx = std::sin(mid);
        const float my = -std::cos(mid);
        const float cosHalfSweep = std::cos(std::min(sweep, 360.0f) * 0.5f * kDegToRad);
        const float cap0x = cx + radius * sx;
        const float cap0y = cy + radius * sy;
        const float cap1x = cx + radius * ex;
        const float cap1y = cy + radius * ey;
        const float capReach2 = (half + 0.5f) * (half + 0.5f);

        // Bounding box: the whole ring, or for a part of it its ends and the outermost points it passes
        float bx0 = cx - rOut;
        float bx1 = cx + rOut;
        float by0 = cy - rOut;
        float by1 = cy + rOut;

        if (!closed) {
            bx0 = by0 = 1e9f;
            bx1 = by1 = -1e9f;
            const auto add = [&](const float x, const float y) {
                bx0 = std::min(bx0, x);
                bx1 = std::max(bx1, x);
                by0 = std::min(by0, y);
                by1 = std::max(by1, y);
            };

            for (const float r : {rIn, rOut}) {
                add(cx + r * sx, cy + r * sy);
                add(cx + r * ex, cy + r * ey);
            }

            for (int k = 0; k < 4; ++k) {
                float rel = static_cast<float>(k) * 90.0f - start0;
                rel += rel < 0.0f ? 360.0f : 0.0f;

                if (rel <= sweep) {
                    add(cx + rOut * static_cast<float>(k == 1) - rOut * static_cast<float>(k == 3), cy - rOut * static_cast<float>(k == 0) + rOut * static_cast<float>(k == 2));
                }
            }

            const float margin = half + 1.0f; // round caps reach past the ends
            bx0 -= margin;
            bx1 += margin;
            by0 -= margin;
            by1 += margin;
        }

        const int yStart = std::max(top_, pixelFloor(by0));
        const int yEnd = std::min(top_ + height_ - 1, pixelCeil(by1));
        const int boxLeft = std::max(0, pixelFloor(bx0));
        const int boxRight = std::min(width_ - 1, pixelCeil(bx1));

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
                const int xStart = std::max(boxLeft, pixelFloor(from));
                const int xEnd = std::min(boxRight, pixelCeil(to));

                for (int x = xStart; x <= xEnd; ++x) {
                    const float dx = static_cast<float>(x) + 0.5f - cx;
                    const float r = std::sqrt(dx * dx + dy * dy);
                    const float radial = std::fabs(r - radius) - half;

                    // Caps sit inside the radial band, so nothing outside it can be covered
                    if (radial >= 0.5f) {
                        continue;
                    }

                    float d = radial;
                    float t = 0.0f;

                    if (closed || dx * mx + dy * my >= r * cosHalfSweep) {
                        if (Gradient) {
                            float rel = fastAtan2Deg(dx, -dy) - start0;
                            rel += rel < 0.0f ? 360.0f : 0.0f;
                            rel += rel < 0.0f ? 360.0f : 0.0f;
                            if (closed) {
                                t = rel / 360.0f;
                            }
                            else if (rel <= sweep) {
                                t = sweep > 0.0f ? rel / sweep : 0.0f;
                            }
                            else {
                                t = rel - sweep < 360.0f - rel ? 1.0f : 0.0f; // right on an end, rounded past it
                            }
                        }
                    }
                    else if (roundCaps) {
                        const float d0x = dx + cx - cap0x;
                        const float d0y = dy + cy - cap0y;
                        const float d1x = dx + cx - cap1x;
                        const float d1y = dy + cy - cap1y;
                        const float q0 = d0x * d0x + d0y * d0y;
                        const float q1 = d1x * d1x + d1y * d1y;

                        if (std::min(q0, q1) >= capReach2) {
                            continue; // too far from both caps
                        }

                        d = std::sqrt(std::min(q0, q1)) - half;
                        t = q0 < q1 ? 0.0f : 1.0f;
                    }
                    else {
                        // Flat ends: distance to the line through the end, as long as the pixel is less than
                        // 90 degrees past it (r * sin of the angle past the end)
                        const float before = sx * dy - sy * dx; // r * sin(angle - a0), negative before the start
                        const float after = ex * dy - ey * dx;  // r * sin(angle - a1), positive past the end
                        const float pastStart = before <= 0.0f && sx * dx + sy * dy > 0.0f ? -before : r;
                        const float pastEnd = after >= 0.0f && ex * dx + ey * dy > 0.0f ? after : r;
                        d = std::max(radial, std::min(pastStart, pastEnd));
                        t = pastStart < pastEnd ? 0.0f : 1.0f;
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
        if (!finite({x0, y0, x1, y1}) || !drawing()) {
            return;
        }

        const int yStart = std::max(top_, pixelFloor(y0) - 1);
        const int yEnd = std::min(top_ + height_ - 1, pixelCeil(y1) + 1);
        const int xStart = std::max(0, pixelFloor(x0) - 1);
        const int xEnd = std::min(width_ - 1, pixelCeil(x1) + 1);

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
        shadeArc<false>(cx, cy, radius, width, a0, a1, roundCaps, [c](float) { return c; });
    }

    void Painter::arcGradient(const float cx, const float cy, const float radius, const float width, const float a0, const float a1, const Color c0, const Color c1, const bool roundCaps) {
        shadeArc<true>(cx, cy, radius, width, a0, a1, roundCaps, [c0, c1](const float t) { return mix(c0, c1, t); });
    }

    void Painter::circle(const float cx, const float cy, const float radius, const float width, const Color c) {
        arc(cx, cy, radius, width, 0.0f, 360.0f, c, false);
    }

    void Painter::disc(const float cx, const float cy, const float radius, const Color c) {
        if (!visible(cy - radius - 1.0f, cy + radius + 1.0f)) {
            return;
        }

        fillShape(cx - radius, cy - radius, cx + radius, cy + radius, c, [=](const float x, const float y) { return std::sqrt((x - cx) * (x - cx) + (y - cy) * (y - cy)) - radius; });
    }

    void Painter::roundRect(const float x0, const float y0, const float x1, const float y1, const float radius, const Color c) {
        if (!visible(y0 - 1.0f, y1 + 1.0f)) {
            return;
        }

        const float cx = (x0 + x1) * 0.5f;
        const float cy = (y0 + y1) * 0.5f;
        const float hx = (x1 - x0) * 0.5f - radius;
        const float hy = (y1 - y0) * 0.5f - radius;

        fillShape(x0, y0, x1, y1, c, [=](const float x, const float y) {
            const float qx = std::max(std::fabs(x - cx) - hx, 0.0f);
            const float qy = std::max(std::fabs(y - cy) - hy, 0.0f);
            return std::sqrt(qx * qx + qy * qy) - radius;
        });
    }

    void Painter::block(const int x0, const int y0, const int x1, const int y1, const Color c) {
        if (!drawing()) {
            return;
        }

        for (int y = std::max(y0, top_); y < std::min(y1, top_ + height_); ++y) {
            for (int x = std::max(x0, 0); x < std::min(x1, width_); ++x) {
                blendFixed(x, y, c, 256);
            }
        }
    }

    void Painter::line(const float x0, const float y0, const float x1, const float y1, const float width, const Color c) {
        const float half = width * 0.5f;

        if (!visible(std::min(y0, y1) - half - 1.0f, std::max(y0, y1) + half + 1.0f)) {
            return;
        }

        const float vx = x1 - x0;
        const float vy = y1 - y0;
        const float len2 = vx * vx + vy * vy;
        const float inv = len2 > 0.0f ? 1.0f / len2 : 0.0f;

        fillShape(std::min(x0, x1) - half, std::min(y0, y1) - half, std::max(x0, x1) + half, std::max(y0, y1) + half, c, [=](const float x, const float y) {
            const float wx = x - x0;
            const float wy = y - y0;
            const float t = clamp01((wx * vx + wy * vy) * inv);
            const float dx = wx - vx * t;
            const float dy = wy - vy * t;
            return std::sqrt(dx * dx + dy * dy) - half;
        });
    }

    void Painter::tick(const float cx, const float cy, const float angle, const float r0, const float r1, const float width, const Color c) {
        line(px(cx, r0, angle), py(cy, r0, angle), px(cx, r1, angle), py(cy, r1, angle), width, c);
    }

    void Painter::mask(const float cx, const float cy, const float radius, const float feather) {
        if (!finite({cx, cy, radius, feather}) || !drawing()) {
            return;
        }

        // Per row: inside the circle nothing happens, beyond radius + feather the pixels turn black,
        // only the pixels in between need their distance (a square root each)
        const float outer = radius + std::max(feather, 0.0f);

        for (int y = top_; y < top_ + height_; ++y) {
            const float dy = static_cast<float>(y) + 0.5f - cy;
            uint16_t* row = buffer_ + (y - top_) * width_;
            const float in2 = radius * radius - dy * dy;
            const float out2 = outer * outer - dy * dy;
            // Pixel centres x + 0.5 with |x + 0.5 - cx| < reach lie within that distance of the centre
            const float inReach = radius > 0.0f && in2 > 0.0f ? std::sqrt(in2) : -1.0f;
            const float outReach = outer > 0.0f && out2 > 0.0f ? std::sqrt(out2) : -1.0f;

            for (int x = 0; x < width_; ++x) {
                const float dx = std::fabs(static_cast<float>(x) + 0.5f - cx);

                if (dx < inReach) {
                    x = std::max(x, pixelFloor(cx + inReach - 0.5f) - 1); // skip the inside of the circle
                    continue;
                }

                if (dx >= outReach) {
                    row[x] = 0; // black (also with swapped bytes)
                    continue;
                }

                const float d = std::sqrt(dx * dx + dy * dy) - radius;

                if (d > 0.0f) {
                    blend(x, y, 0, feather > 0.0f ? clamp01(d / feather) : 1.0f);
                }
            }
        }
    }

    void Painter::text(const Font* font, const char* s, const float x, const float y, const Color c, const Align align) {
        if (font == nullptr || s == nullptr || *s == '\0' || !finite({x, y}) || !drawing()) {
            return;
        }

        if (textObserver != nullptr) {
            textObserver(font, s, x, y, align);
        }

        const int baseline = pixelFloor(y + 0.5f);

        if (baseline + font->size() < top_ || baseline - 2 * font->size() >= top_ + height_) {
            return;
        }

        // Positions as LovyanGFX's drawString placed its VLW fonts
        int pen = pixelFloor(x + 0.5f) + font->leftOverhang(s);

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
