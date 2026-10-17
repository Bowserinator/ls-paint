#!/usr/bin/env bash
# Draws the ruler digit sheets in this directory. Run it after changing them;
# the canvas ruler reads its numbers a digit at a time from these files.
#
# Each sheet holds the digits 0 to 9 in order, one cell each: digits.png for the
# strip along the top edge, and digits-turned.png, whose cells are drawn a
# quarter turn round, for the strip down the left edge. The cells are the same
# size in both, so the left sheet's are as wide as the top sheet's are tall.
# The digits are drawn white: the UI tints them with the color it draws its
# labels in.
set -euo pipefail
out="$(cd "$(dirname "$0")" && pwd)"
sans="$out/../../third_party/fonts/DejaVuSans.ttf"
pointsize=9
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

# A digit is drawn on a canvas roomy enough for the whole face and then trimmed
# to its own ink, so no cell ever cuts a glyph.
for digit in 0 1 2 3 4 5 6 7 8 9; do
    magick -size 64x64 xc:none -font "$sans" -pointsize "$pointsize" \
        -fill white -stroke none -gravity center -annotate 0 "$digit" \
        -trim +repage "$tmp/ink-$digit.png"
done

# One cell size for every digit: the widest and the tallest, and a pixel of
# margin on each side.
cell_w=0
cell_h=0
for ink in "$tmp"/ink-?.png; do
    size="$(magick identify -format '%w %h' "$ink")"
    w="${size%% *}"
    h="${size##* }"
    if [ "$w" -gt "$cell_w" ]; then cell_w="$w"; fi
    if [ "$h" -gt "$cell_h" ]; then cell_h="$h"; fi
done
cell_w=$((cell_w + 2))
cell_h=$((cell_h + 2))

for digit in 0 1 2 3 4 5 6 7 8 9; do
    magick "$tmp/ink-$digit.png" -background none -gravity center \
        -extent "${cell_w}x${cell_h}" "$tmp/flat-$digit.png"
    magick "$tmp/flat-$digit.png" -background none -rotate 90 \
        "$tmp/turn-$digit.png"
done

magick "$tmp"/flat-?.png +append "$out/digits.png"
magick "$tmp"/turn-?.png +append "$out/digits-turned.png"
echo "Ruler digit cells are ${cell_w}x${cell_h} pixels."
