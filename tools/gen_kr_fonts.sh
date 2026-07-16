#!/bin/bash
# Generate subsetted Korean LVGL fonts (Pretendard) for the info panel.
# Only the glyphs the UI actually renders are included, so each font stays tiny.
# Includes ASCII (0x20-0x7E) + degree sign so temperatures show "25°".
#
# Requires: npx (lv_font_conv fetched on demand) and Pretendard OTFs in
# ~/Library/Fonts. Re-run after adding any new Korean string to the UI.
set -e
cd "$(dirname "$0")/.."
OUT=firmware/src
SB="$HOME/Library/Fonts/Pretendard-SemiBold.otf"
MD="$HOME/Library/Fonts/Pretendard-Medium.otf"

# Every Korean glyph used anywhere in the UI (weather conditions, AQI
# categories, stat labels, titles, clock/calendar words). Dedup is automatic.
SYMS="°날씨기온대기질맑음대체로부분흐림안개이슬비어는눈진깨소나기뇌우박알수없체감습도강수최고저좋보통나쁨매우위험초미세먼지오전후년월일화목금토현재오늘삼성자평단원"

gen() { # size weight-file outname
  echo "  font_$3 (size $1)"
  npx --yes lv_font_conv@latest --font "$2" \
    --size "$1" --bpp 4 --format lvgl --no-compress --lv-include lvgl.h \
    -r 0x20-0x7E --symbols "$SYMS" \
    -o "$OUT/font_$3.c" --force-fast-kern-format
}

echo "Generating Korean fonts..."
gen 60 "$SB" kr_60
gen 40 "$SB" kr_40
gen 26 "$SB" kr_26
gen 18 "$MD" kr_18
gen 14 "$MD" kr_14
echo "Done. Generated font_kr_{60,40,26,18,14}.c"
