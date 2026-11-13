#!/bin/bash
# Regenerates the theme fonts from assets/fonts (Fontsource woff subsets, OFL) with lv_font_conv.
# Text: Latin-1, general punctuation (dashes, quotes, bullet, ellipsis), euro, trademark, all of Cyrillic, numero sign.
set -e
cd "$(dirname "$0")/.."
F=assets/fonts
FA=third_party/lvgl/scripts/built_in_font/FontAwesome5-Solid+Brands+Regular.woff
ICONS=0xf04b,0xf04c,0xf021,0xf067,0xf068,0xf0f3,0xf001,0xf030,0xf0e7,0xf00c,0xf00d,0xf004,0xf2db
LATIN=0x20-0x7E,0xA0-0xFF,0x2010-0x2027,0x2030-0x203A,0x20AC,0x2122,0x2191,0x2193,0x2212
CYR=0x400-0x45F,0x490-0x491,0x2116
conv() { npx -y lv_font_conv@1.5.3 --no-compress --no-prefilter --bpp 4 --format lvgl --lv-include lvgl.h "$@"; }
text() { # name size family weight [triangles]
  # the terminal's button hints: Font Awesome's carets, placed at the triangles' code points (theme.h TH_UP...)
  local extra=(); [ -n "$5" ] && extra=(--font $FA -r "0xf0d8=>0x25B2,0xf0d7=>0x25BC,0xf0d9=>0x25C0,0xf0da=>0x25B6")
  conv --size $2 --font $F/$3-latin-$4.woff -r $LATIN --font $F/$3-cyrillic-$4.woff -r $CYR --font $FA -r $ICONS "${extra[@]}" \
       --lv-font-name $1 -o ui/fonts/$1.c
}
caps() { conv --size $2 --font $F/$3-latin-$4.woff -r 0x20,0x2D,0x2E,0x41-0x5A --font $F/$3-cyrillic-$4.woff -r 0x401,0x410-0x42F --lv-font-name $1 -o ui/fonts/$1.c; } # the badge name
title() { conv --size $2 --font $F/$3-latin-$4.woff -r 0x20-0x5F,0xAB,0xBB,0x2013,0x2014,0x2026 --font $F/$3-cyrillic-$4.woff -r 0x401,0x410-0x42F --lv-font-name $1 -o ui/fonts/$1.c; } # capitals for big titles
digits() { conv --size $2 --font $F/$3-latin-$4.woff -r 0x25,0x2E,0x30-0x3A --lv-font-name $1 -o ui/fonts/$1.c; }
text font_instr_20 20 jetbrains-mono 400
text font_instr_28 28 jetbrains-mono 700
digits font_instr_96 96 jetbrains-mono 700
caps font_instr_56 56 jetbrains-mono 700
title font_instr_40 40 jetbrains-mono 700
text font_diag_16 16 ibm-plex-mono 500 tri
text font_diag_16b 16 ibm-plex-mono 600 tri
text font_diag_20 20 ibm-plex-mono 500 tri
text font_diag_20b 20 ibm-plex-mono 600 tri
text font_diag_28 28 ibm-plex-mono 600
digits font_diag_96 96 ibm-plex-mono 600
caps font_diag_56 56 ibm-plex-mono 600
title font_diag_40 40 ibm-plex-mono 600
