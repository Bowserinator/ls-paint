#!/usr/bin/env bash
# Draws the shape outline and fill texture samples in this directory. Run it
# after changing a sample; the Outline and Fill menus in the ribbon show these
# files.
set -euo pipefail
out="$(cd "$(dirname "$0")" && pwd)"
size=32
red="#c0392b"
deep="#96281b"

# swatch <name> <magick arguments...> draws one 32x32 texture sample.
swatch() {
    local name="$1"; shift
    magick -size ${size}x${size} xc:none "$@" "$out/$name.png"
}

# No outline: a hollow square crossed out.
swatch no-outline \
    -fill none -stroke "#5a5a5a" -strokewidth 3 -draw "rectangle 6,6 26,26" \
    -fill none -stroke "$red" -strokewidth 4 -draw "line 4,28 28,4"

# No fill: the same square, empty inside, crossed out.
swatch no-fill \
    -fill "#f2f2f2" -stroke "#5a5a5a" -strokewidth 2 -draw "rectangle 6,6 26,26" \
    -fill none -stroke "$red" -strokewidth 4 -draw "line 4,28 28,4"

# Crayon: a grainy band with paper showing through it.
swatch crayon \
    -fill none -stroke "$red" -strokewidth 8 -draw "line 3,12 29,9" \
    -fill none -stroke "$deep" -strokewidth 7 -draw "line 3,21 29,24" \
    -fill white -stroke none -draw "rectangle 8,4 12,13" -draw "rectangle 19,19 23,27" \
    -spread 1.4

# Marker: even strokes with soft edges that overlap a little.
swatch marker \
    -fill none -stroke "$red" -strokewidth 5 -draw "line 2,8 30,8" \
    -fill none -stroke "$deep" -strokewidth 5 -draw "line 2,16 30,16" \
    -fill none -stroke "$red" -strokewidth 5 -draw "line 2,24 30,24" \
    -blur 0x0.4

# Oil: thick, smooth strokes laid down side by side.
swatch oil \
    -fill none -stroke "$deep" -strokewidth 10 -draw "line 3,10 29,8" \
    -fill none -stroke "$red" -strokewidth 9 -draw "line 3,20 29,23" \
    -blur 0x0.6

# Natural pencil: thin, slightly broken strokes.
swatch natural-pencil \
    -fill none -stroke "#4a4a4a" -strokewidth 1.5 -draw "line 3,8 28,6" \
    -fill none -stroke "#3a3a3a" -strokewidth 1.5 -draw "line 4,13 27,14" \
    -fill none -stroke "#4a4a4a" -strokewidth 1.5 -draw "line 3,19 28,21" \
    -fill none -stroke "#3a3a3a" -strokewidth 1.5 -draw "line 5,25 26,24" \
    -spread 0.8

# Watercolor: a soft wash with a darker centre.
swatch watercolor \
    -fill "#e6b0aa" -stroke none -draw "ellipse 16,16 14,11 0,360" \
    -fill "$red" -draw "ellipse 16,16 8,6 0,360" \
    -blur 0x1.8
