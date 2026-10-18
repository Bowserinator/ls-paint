#!/usr/bin/env bash
# Regenerates the rotate badge: a quarter-turn arrow with a head at each end,
# one file per quadrant of the rotation centre. Each arrow is drawn dark over a
# white outline, so it reads on both light and dark pixels of the canvas.
set -euo pipefail

out="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

# Drawn four times the size it is used at, then scaled down for smooth edges.
# The arc runs from the top of the circle round to its right, so the base file
# is the top right quadrant; the others are it turned a quarter turn further.
magick -size 96x96 xc:none \
    -stroke white -strokewidth 16 -fill none \
    -draw "path 'M 48 18 A 30 30 0 0 1 78 48'" \
    -stroke white -strokewidth 8 -fill white \
    -draw "path 'M 48 9 L 66 18 L 48 27 Z'" \
    -draw "path 'M 69 48 L 78 66 L 87 48 Z'" \
    -stroke '#303030' -strokewidth 8 -fill none \
    -draw "path 'M 48 18 A 30 30 0 0 1 78 48'" \
    -stroke none -fill '#303030' \
    -draw "path 'M 48 9 L 66 18 L 48 27 Z'" \
    -draw "path 'M 69 48 L 78 66 L 87 48 Z'" \
    "$tmp/base.png"

declare -A turns=([ne]=0 [nw]=270 [se]=90 [sw]=180)
for quadrant in ne nw se sw; do
    magick "$tmp/base.png" -background none -rotate "${turns[$quadrant]}" \
        -resize 24x24 "$out/rotate-$quadrant.png"
done

echo "Wrote the four quadrant files: rotate-ne.png, rotate-nw.png, rotate-se.png, rotate-sw.png."
