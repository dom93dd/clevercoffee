/**
 * @file RoundDisplayFonts.h
 *
 * @brief Anti-aliased fonts (Barlow Semi Condensed, SIL OFL 1.1) as VLW data in flash.
 *        Regenerate with lib/RoundDisplay/tools/build_fonts.sh.
 */

#pragma once

#include <LovyanGFX.hpp>

namespace rd::fonts {

    bool load();

    /** 80 px, digits and ",.-/" only, tabular digits */
    const lgfx::IFont* big();

    /** 34 px, digits and "+-,./%:gsK°", tabular digits */
    const lgfx::IFont* mid();

    /** 20 px, ASCII plus German umlauts */
    const lgfx::IFont* text();

    /** 19 px, capitals and digits with wide letter spacing */
    const lgfx::IFont* label();

    /** Height of the digits of big() above the baseline */
    constexpr int kBigDigitHeight = 56;

} // namespace rd::fonts
