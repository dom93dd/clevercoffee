#!/bin/sh
# Regenerates the font headers in src/fonts from the TTF files in fonts/.
# Needs Python 3 with freetype-py (pip install freetype-py).
set -e
cd "$(dirname "$0")/.."
PY="${PYTHON:-python3}"
GEN="$PY tools/make_font.py"
MEDIUM=fonts/BarlowSemiCondensed-Medium.ttf
SEMIBOLD=fonts/BarlowSemiCondensed-SemiBold.ttf
TEXT_CHARS="--chars @ascii --chars @de --chars áéíóúñÁÉÍÓÚÑ¿¡Ø"

# Large numbers: temperature, shot timer
$GEN --ttf $MEDIUM --size 80 --tabular --chars "0123456789,.-/" --name rd_font_big --out src/fonts/fontBig.h
# Secondary numbers: alarm temperature, standby, units next to the big numbers
$GEN --ttf $SEMIBOLD --size 34 --tabular --chars "0123456789,.-+/%:gsK°" --name rd_font_mid --out src/fonts/fontMid.h
# Second number under the big one while brewing with the scale (time or weight)
$GEN --ttf $SEMIBOLD --size 32 --tabular --chars "0123456789,.-gs" --name rd_font_mid_small --out src/fonts/fontMidSmall.h
# Running text: messages, temperatures and last shot below the big number
$GEN --ttf $MEDIUM --size 20 $TEXT_CHARS --name rd_font_text --out src/fonts/fontText.h
# Line right under the big number (setpoint, target) and long messages
$GEN --ttf $MEDIUM --size 19 $TEXT_CHARS --name rd_font_text_small --out src/fonts/fontTextSmall.h
# Message lines that would come too close to the ring in the text font
$GEN --ttf $MEDIUM --size 18 $TEXT_CHARS --name rd_font_text_compact --out src/fonts/fontTextCompact.h
# Hints at the bottom of a screen (alarms, no WiFi, refill): smaller than the running text
$GEN --ttf $MEDIUM --size 16 --chars "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789" --chars "ÄÖÜäöüß° .,:;-!?/()%'+<" --name rd_font_hint --out src/fonts/fontHint.h
# Status labels in capitals with wider letter spacing
$GEN --ttf $SEMIBOLD --size 19 --tracking 2 --tabular --chars "ABCDEFGHIJKLMNOPQRSTUVWXYZÄÖÜÁÉÍÓÚÑÀÈÌÒÙ0123456789.,-/·:%+!?'&" --name rd_font_label --out src/fonts/fontLabel.h
