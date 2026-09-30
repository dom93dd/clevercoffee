#!/bin/sh
# Regenerates the VLW font headers in src/fonts from the TTF files in fonts/.
# Needs Python 3 with freetype-py (pip install freetype-py).
set -e
cd "$(dirname "$0")/.."
PY="${PYTHON:-python3}"
GEN="$PY tools/make_vlw_font.py"
MEDIUM=fonts/BarlowSemiCondensed-Medium.ttf
SEMIBOLD=fonts/BarlowSemiCondensed-SemiBold.ttf

# Large numbers: temperature, shot timer
$GEN --ttf $MEDIUM --size 80 --tabular --chars "0123456789,.-/" --name rd_font_big --out src/fonts/fontBig.h
# Secondary numbers: weight, target values, units
$GEN --ttf $SEMIBOLD --size 34 --tabular --chars "0123456789,.-+/%:gsK°" --name rd_font_mid --out src/fonts/fontMid.h
# Running text: hints, messages, IP addresses
$GEN --ttf $MEDIUM --size 20 --chars @ascii --chars @de --chars "áéíóúñÁÉÍÓÚÑ¿¡" --name rd_font_text --out src/fonts/fontText.h
# Status labels in capitals with wider letter spacing
$GEN --ttf $SEMIBOLD --size 19 --tracking 2 --tabular --chars "ABCDEFGHIJKLMNOPQRSTUVWXYZÄÖÜÁÉÍÓÚÑÀÈÌÒÙ0123456789.,-/·:%+!?'&" --name rd_font_label --out src/fonts/fontLabel.h
