/**
 * @file RoundDisplayFont.cpp
 */

#include "RoundDisplayFont.h"

#include <algorithm>

namespace rd {

    bool Font::valid() const {
        return data_ != nullptr && data_[0] == 'R' && data_[1] == 'D' && data_[2] == 1 && count() > 0;
    }

    bool Font::find(const uint32_t code, Glyph& glyph) const {
        // Records are sorted by code point
        int lo = 0;
        int hi = count() - 1;

        while (lo <= hi) {
            const int mid = (lo + hi) / 2;
            const uint8_t* r = data_ + kHeader + mid * kRecord;
            const uint32_t c = r[0] | r[1] << 8;

            if (c < code) {
                lo = mid + 1;
            }
            else if (c > code) {
                hi = mid - 1;
            }
            else {
                glyph.width = r[2];
                glyph.height = r[3];
                glyph.advance = r[4];
                glyph.left = static_cast<int8_t>(r[5]);
                glyph.top = static_cast<int8_t>(r[6]);
                glyph.bitmap = data_ + kHeader + count() * kRecord + (r[7] | r[8] << 8 | r[9] << 16);
                return true;
            }
        }

        return false;
    }

    int Font::width(const char* utf8) const {
        Glyph space;
        find(' ', space);
        Glyph g;
        int pen = leftOverhang(utf8);
        int right = pen;

        for (const char* p = utf8; p != nullptr && *p != '\0';) {
            if (!find(next(p), g)) {
                g = space;
            }

            right = pen + std::max<int>(g.advance, g.left + g.width);
            pen += g.advance;
        }

        return right;
    }

    int Font::leftOverhang(const char* utf8) const {
        Glyph g;
        const char* p = utf8;

        if (p == nullptr || *p == '\0' || !find(next(p), g)) {
            return 0;
        }

        return g.left < 0 ? -g.left : 0;
    }

    int Font::inkTop(const char* utf8) const {
        int top = 0;
        Glyph g;

        for (const char* p = utf8; p != nullptr && *p != '\0';) {
            if (find(next(p), g) && g.height > 0 && g.top > top) {
                top = g.top;
            }
        }

        return top;
    }

    uint32_t Font::next(const char*& p) {
        const auto b0 = static_cast<uint8_t>(*p++);

        if (b0 < 0x80) {
            return b0;
        }

        int extra = 0;
        uint32_t code = 0;

        if ((b0 & 0xE0) == 0xC0) {
            extra = 1;
            code = b0 & 0x1F;
        }
        else if ((b0 & 0xF0) == 0xE0) {
            extra = 2;
            code = b0 & 0x0F;
        }
        else if ((b0 & 0xF8) == 0xF0) {
            extra = 3;
            code = b0 & 0x07;
        }
        else {
            return b0;
        }

        for (int i = 0; i < extra; ++i) {
            const auto b = static_cast<uint8_t>(*p);

            if ((b & 0xC0) != 0x80) {
                return b0; // cut off sequence
            }

            code = code << 6 | (b & 0x3F);
            ++p;
        }

        return code;
    }

} // namespace rd
