#!/usr/bin/env bash
# Draws the brush gallery thumbnails in this directory. Run it after changing a
# brush picture; the picker in the ribbon shows these files.
set -euo pipefail
out="$(cd "$(dirname "$0")" && pwd)"
size=78
mark="#e2762f"

# hand <name> <extra magick arguments...> draws the wooden handle every brush
# shares, then whatever the caller adds.
hand() {
    local name="$1"; shift
    magick -size ${size}x${size} xc:none \
        -fill "#c7883c" -stroke "#8a5a22" -strokewidth 2 \
        -draw "translate 50,26 rotate -45 rectangle -21,-4 21,4" \
        -fill "#b9bec4" -stroke "#7d858d" -strokewidth 2 \
        -draw "translate 50,26 rotate -45 rectangle -14,-5 -8,5" \
        "$@" "$out/$name.png"
}

hand brush -fill none -stroke "$mark" -strokewidth 5 \
    -draw "path 'M 8,70 C 16,60 24,74 34,64'" \
    -fill "#d8c9a8" -stroke "#8a7a58" -strokewidth 2 \
    -draw "ellipse 27,47 12,14 130,310" -draw "ellipse 24,45 9,12 140,300"

hand calligraphy-1 -fill none -stroke "$mark" -strokewidth 5 \
    -draw "path 'M 7,68 C 15,62 28,68 40,52'" \
    -fill "#4d4d4d" -stroke "#2f2f2f" -strokewidth 2 \
    -draw "translate 26,48 rotate -45 polygon -15,-4 15,-4 19,5 -11,5"

hand calligraphy-2 -fill none -stroke "$mark" -strokewidth 5 \
    -draw "path 'M 6,64 C 18,66 30,58 42,42'" \
    -fill "#5a5a5a" -stroke "#333" -strokewidth 2 \
    -draw "translate 29,47 rotate -70 polygon -10,-2 10,-2 13,3 -7,3"

hand airbrush -fill "#9aa2ab" -stroke "#6d757d" -strokewidth 2 \
    -draw "translate 32,44 rotate -45 polygon -14,-4 6,-4 6,4 -14,4" \
    -fill "$mark" -stroke none \
    -draw "circle 20,52 23,52" -draw "circle 12,60 15,60" -draw "circle 26,60 29,60" \
    -draw "circle 8,70 11,70" -draw "circle 20,70 23,70" -draw "circle 32,68 34,68"

hand oil -fill none -stroke "$mark" -strokewidth 6 \
    -draw "path 'M 7,68 C 15,56 22,72 32,62'" \
    -fill "#d6b877" -stroke "#8d7539" -strokewidth 2 \
    -draw "translate 28,46 rotate -45 polygon -12,-9 6,-9 6,9 -12,9"

hand crayon -fill none -stroke "$mark" -strokewidth 7 \
    -draw "path 'M 6,70 C 12,58 20,72 28,60 L 34,66'" \
    -fill "#e0524a" -stroke "#9c332d" -strokewidth 2 \
    -draw "translate 30,48 rotate -45 polygon -16,-9 8,-9 12,9 -12,9"

hand marker -fill none -stroke "$mark" -strokewidth 6 \
    -draw "path 'M 8,66 L 40,54'" \
    -fill "#8f959c" -stroke "#5f666d" -strokewidth 2 \
    -draw "translate 28,46 rotate -45 polygon -10,-10 4,-6 6,6 -6,10"

hand natural-pencil -fill none -stroke "$mark" -strokewidth 3 \
    -draw "path 'M 6,66 C 16,60 26,72 40,60'" \
    -fill "#e0b877" -stroke "#8a6a34" -strokewidth 2 \
    -draw "translate 24,50 rotate -45 polygon -6,-7 6,-7 6,7 -6,7" \
    -fill "#3a3a3a" -stroke "#202020" \
    -draw "translate 36,38 rotate -45 polygon -4,-5 8,-5 8,5 -4,5"

hand watercolor -fill none -stroke "$mark" -strokewidth 4 \
    -draw "path 'M 6,66 C 14,58 26,70 38,62'" \
    -fill "#7fb4d8" -stroke "#4d84a8" -strokewidth 2 \
    -draw "ellipse 24,46 14,12 0,360" -fill "#bcd9ec" -stroke none \
    -draw "ellipse 24,46 9,8 0,360" -draw "circle 10,72 13,72"
