#!/usr/bin/env bash
# Draws the glyphs the status bar shows beside each of its readings. Every one
# is a dark shape on a transparent ground, so the cells keep their light face.
set -euo pipefail

out="$(cd "$(dirname "$0")" && pwd)"

size=24
ink="#4d4d4d"
red="#c02020"
green="#1f9a1f"
blue="#2040c0"

# Draws one glyph and centres its ink in the canvas, so the pictures line up
# with each other and with the text they sit beside.
glyph() {
    local name="$1"
    shift
    magick "$@" -trim +repage -background none -gravity center \
        -extent ${size}x${size} "$out/$name.png"
}

# Where the pointer is: an arrow.
glyph pointer -size ${size}x${size} xc:none -fill "$ink" -stroke none \
    -draw "polygon 7,3 7,19 11,15 14,21 17,20 14,14 19,14"

# How big the selection is: a dashed box.
glyph selection -size ${size}x${size} xc:none -fill none -stroke "$ink" -strokewidth 2 \
    -draw "stroke-dasharray 3,2 rectangle 3,5 20,19"

# How big the paper is: a sheet with a turned corner.
glyph canvas -size ${size}x${size} xc:none -fill none -stroke "$ink" -strokewidth 2 \
    -draw "path 'M 5,3 L 15,3 L 20,8 L 20,21 L 5,21 Z'" \
    -draw "path 'M 15,3 L 15,8 L 20,8'"

# The colour under the pointer: three chips, one per channel.
glyph color -size ${size}x${size} xc:none -stroke none \
    -fill "$red" -draw "circle 9,9 9,4" \
    -fill "$green" -draw "circle 15,9 15,4" \
    -fill "$blue" -draw "circle 12,16 12,11"

# The zoom: a magnifier.
glyph zoom -size ${size}x${size} xc:none -fill none -stroke "$ink" -strokewidth 2 \
    -draw "circle 10,10 10,4" \
    -draw "line 15,15 21,21"

echo "Status glyphs are ${size}x${size} pixels, ink centred."
