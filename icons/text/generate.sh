#!/usr/bin/env bash
# Draws the text background selector icon in this directory. Run it after
# changing it; the Background group in the Text tab shows this file.
set -euo pipefail
out="$(cd "$(dirname "$0")" && pwd)"
serif="$out/../../third_party/fonts/DejaVuSerif.ttf"
size=32
blue="#3b78d8"
edge="#404040"

# A blue square with a dashed, open square over its top left corner, holding a
# serif A. The button that shows it paints the color the text is filled with
# behind the open square, or shows the blue one through it.
magick -size ${size}x${size} xc:none \
    -fill "$blue" -stroke none -draw "rectangle 14,14 30,30" \
    -fill none -stroke "$edge" -strokewidth 2 \
    -draw "stroke-dasharray 3,3 rectangle 3,3 19,19" \
    -fill black -stroke none -font "$serif" -pointsize 14 \
    -draw "text 7,16 'A'" \
    "$out/background-frame.png"
