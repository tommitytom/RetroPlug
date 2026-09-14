#!/usr/bin/env bash
#
# Regenerate the monospaced UI fonts in packages/native/src/fonts/.
#
# RetroPlug's UI has no font-family: a component picks a font by PIXEL SIZE, the TS
# style pipe turns that size into an index (lv_binding_js .../style/pipe/text.ts), and
# native maps the index to an `lv_font_montserrat_*` pointer (.../style/font/font.hpp).
# So the only way to change the face without teaching the whole stack about families is
# to change what those symbols POINT AT.
#
# That is what this does. It builds DejaVu Sans Mono into the `lv_font_montserrat_*`
# symbol names, and lv_conf.h sets every LV_FONT_MONTSERRAT_* to 0 so LVGL stops
# compiling its own. The names still say montserrat because font.hpp (a submodule we
# would otherwise have to fork) hardcodes them; nothing else about them is Montserrat.
#
# Arguments mirror LVGL's own scripts/built_in_font/built_in_font_gen.py, which is
# Python and therefore not run here. The FontAwesome pass is NOT optional: the binding's
# dropdownlist.cpp draws LV_SYMBOL_* glyphs, and those live in these same font files.
#
# Fonts are licensed for redistribution: DejaVu (Bitstream Vera derivative, permissive)
# and Font Awesome 5 Free (SIL OFL 1.1). See deps/.../built_in_font/font_license.

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

SRC_FONT="tools/fonts/DejaVuSansMono.ttf"
AWESOME="deps/dpf.js/deps/lv_binding_js/deps/lvgl/scripts/built_in_font/FontAwesome5-Solid+Brands+Regular.woff"
OUT_DIR="packages/native/src/fonts"

# The sizes lv_conf.h enables; font.hpp maps every other requested size onto one of them.
SIZES="12 14 16 18 22 24 32"

# The built-in symbol glyphs, verbatim from LVGL's built_in_font_gen.py.
SYMS="61441,61448,61451,61452,61453,61457,61459,61461,61465,61468,61473,61478,61479,61480,61502,61507,61512,61515,61516,61517,61521,61522,61523,61524,61543,61544,61550,61552,61553,61556,61559,61560,61561,61563,61587,61589,61636,61637,61639,61641,61664,61671,61674,61683,61724,61732,61787,61931,62016,62017,62018,62019,62020,62087,62099,62212,62189,62810,63426,63650"

for f in "$SRC_FONT" "$AWESOME"; do
  [ -f "$f" ] || { echo "missing: $f" >&2; exit 1; }
done
mkdir -p "$OUT_DIR"

for sz in $SIZES; do
  out="$OUT_DIR/lv_font_montserrat_$sz.c"
  echo "generating $out"

  npx -y lv_font_conv@1.5.3 \
    --no-compress --no-prefilter --bpp 4 --size "$sz" \
    --font "$SRC_FONT" -r '0x20-0x7F,0xB0,0x2022' \
    --font "$AWESOME" -r "$SYMS" \
    --format lvgl -o "$out" --force-fast-kern-format

  # lv_font_conv derives BOTH the symbol and an include guard from the output filename,
  # so the file it writes is wrapped in `#if LV_FONT_MONTSERRAT_<sz>` - the very macro
  # lv_conf.h has just set to 0 to switch LVGL's own copy off. Left alone, the whole file
  # would preprocess away and the link would fail on a missing symbol. Rename the guard
  # (uppercase) and leave the symbol (lowercase) alone; C is case-sensitive, so this is
  # exact rather than lucky.
  sed -i "s/\bLV_FONT_MONTSERRAT_$sz\b/RP_FONT_MONO_$sz/g" "$out"
done

echo
echo "done - $(echo $SIZES | wc -w) fonts in $OUT_DIR"
