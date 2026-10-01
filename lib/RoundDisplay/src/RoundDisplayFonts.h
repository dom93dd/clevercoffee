/**
 * @file RoundDisplayFonts.h
 *
 * @brief Anti-aliased fonts (Barlow Semi Condensed, SIL OFL 1.1) in flash.
 *        Regenerate with lib/RoundDisplay/tools/build_fonts.sh.
 */

#pragma once

#include "RoundDisplayFont.h"

namespace rd::fonts {

    /** Checks the font data */
    bool load();

    /** 80 px, digits and ",.-/" only, tabular digits */
    const Font* big();

    /** 34 px, digits and "+-,./%:gsK°", tabular digits */
    const Font* mid();

    /** 32 px, digits and ",.-gs", tabular digits: second number under the big one */
    const Font* midSmall();

    /** 20 px, ASCII plus German and Spanish letters */
    const Font* text();

    /** 19 px, same characters as text(): line right under the big number, long messages */
    const Font* textSmall();

    /** 18 px, same characters as text(): message lines that would come too close to the ring */
    const Font* textCompact();

    /** 16 px, hints at the bottom of a screen (letters, digits, umlauts, common punctuation) */
    const Font* hint();

    /** 19 px, capitals and digits with wide letter spacing */
    const Font* label();

    /** Height of the digits of big() above the baseline */
    constexpr int kBigDigitHeight = 56;

} // namespace rd::fonts
