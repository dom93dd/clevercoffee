/**
 * @file RoundDisplayFont.h
 *
 * @brief Anti-aliased bitmap font read straight from flash.
 *
 * Format of tools/make_font.py: 4 bit coverage per pixel and 10 byte glyph records, about
 * half the size of the VLW fonts LovyanGFX reads (8 bit coverage, 28 byte records). The
 * Painter draws the glyphs itself; LovyanGFX only provides sprites and the panel.
 */

#pragma once

#include <cstdint>

namespace rd {

    class Font {
        public:
            struct Glyph {
                    uint8_t width = 0;
                    uint8_t height = 0;
                    uint8_t advance = 0;
                    int8_t left = 0; // bitmap starts this far right of the pen
                    int8_t top = 0;  // bitmap starts this far above the baseline
                    const uint8_t* bitmap = nullptr;

                    /** Coverage 0..15 of a bitmap pixel */
                    uint8_t coverage(const int x, const int y) const {
                        const int i = y * width + x;
                        const uint8_t b = bitmap[i >> 1];
                        return (i & 1) != 0 ? b & 0x0F : b >> 4;
                    }
            };

            explicit constexpr Font(const uint8_t* data) :
                data_(data) {
            }

            /** False if the data is not a font of this format */
            bool valid() const;

            /** Pixel size the font was rendered at */
            int size() const {
                return data_[3];
            }

            int ascent() const {
                return data_[4];
            }

            int descent() const {
                return data_[5];
            }

            /** Glyph of a code point; false if the font does not contain it */
            bool find(uint32_t code, Glyph& glyph) const;

            /**
             * Width as LovyanGFX measures it, so texts sit where they did with its VLW fonts: the
             * advances, except that the last glyph counts up to its right edge if that reaches further,
             * plus leftOverhang(). Characters missing in the font count as a space.
             */
            int width(const char* utf8) const;

            /** How far the first glyph reaches left of the pen (0 if it does not) */
            int leftOverhang(const char* utf8) const;

            /** Height of the ink above the baseline: highest glyph top in the text (0 for spaces only) */
            int inkTop(const char* utf8) const;

            /** Decodes the next UTF-8 character and moves p behind it (invalid bytes come back as they are) */
            static uint32_t next(const char*& p);

        private:
            static constexpr int kHeader = 8;
            static constexpr int kRecord = 10;

            int count() const {
                return data_[6] | data_[7] << 8;
            }

            const uint8_t* data_;
    };

} // namespace rd
