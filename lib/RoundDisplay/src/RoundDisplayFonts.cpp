/**
 * @file RoundDisplayFonts.cpp
 */

#include "RoundDisplayFonts.h"

#include "fonts/fontBig.h"
#include "fonts/fontHint.h"
#include "fonts/fontLabel.h"
#include "fonts/fontMid.h"
#include "fonts/fontMidSmall.h"
#include "fonts/fontText.h"
#include "fonts/fontTextCompact.h"
#include "fonts/fontTextSmall.h"

#include <initializer_list>

namespace rd::fonts {

    namespace {
        constexpr Font bigFont(rd_font_big);
        constexpr Font midFont(rd_font_mid);
        constexpr Font midSmallFont(rd_font_mid_small);
        constexpr Font textFont(rd_font_text);
        constexpr Font textSmallFont(rd_font_text_small);
        constexpr Font textCompactFont(rd_font_text_compact);
        constexpr Font hintFont(rd_font_hint);
        constexpr Font labelFont(rd_font_label);
    } // namespace

    bool load() {
        for (const Font* f : {&bigFont, &midFont, &midSmallFont, &textFont, &textSmallFont, &textCompactFont, &hintFont, &labelFont}) {
            if (!f->valid()) {
                return false;
            }
        }

        return true;
    }

    const Font* big() {
        return &bigFont;
    }

    const Font* mid() {
        return &midFont;
    }

    const Font* midSmall() {
        return &midSmallFont;
    }

    const Font* text() {
        return &textFont;
    }

    const Font* textSmall() {
        return &textSmallFont;
    }

    const Font* textCompact() {
        return &textCompactFont;
    }

    const Font* hint() {
        return &hintFont;
    }

    const Font* label() {
        return &labelFont;
    }

} // namespace rd::fonts
