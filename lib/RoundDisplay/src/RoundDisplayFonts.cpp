/**
 * @file RoundDisplayFonts.cpp
 */

#include "RoundDisplayFonts.h"

#include "fonts/fontBig.h"
#include "fonts/fontLabel.h"
#include "fonts/fontMid.h"
#include "fonts/fontText.h"

namespace rd::fonts {

    namespace {
        lgfx::PointerWrapper bigData(rd_font_big, sizeof(rd_font_big));
        lgfx::PointerWrapper midData(rd_font_mid, sizeof(rd_font_mid));
        lgfx::PointerWrapper textData(rd_font_text, sizeof(rd_font_text));
        lgfx::PointerWrapper labelData(rd_font_label, sizeof(rd_font_label));

        lgfx::VLWfont bigFont;
        lgfx::VLWfont midFont;
        lgfx::VLWfont textFont;
        lgfx::VLWfont labelFont;

        bool loaded = false;
    } // namespace

    bool load() {
        if (!loaded) {
            loaded = bigFont.loadFont(&bigData) && midFont.loadFont(&midData) && textFont.loadFont(&textData) && labelFont.loadFont(&labelData);
        }

        return loaded;
    }

    const lgfx::IFont* big() {
        return &bigFont;
    }

    const lgfx::IFont* mid() {
        return &midFont;
    }

    const lgfx::IFont* text() {
        return &textFont;
    }

    const lgfx::IFont* label() {
        return &labelFont;
    }

} // namespace rd::fonts
