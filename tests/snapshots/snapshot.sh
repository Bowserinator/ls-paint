#!/usr/bin/env bash
# Screenshots the real window at a matrix of window sizes, one PNG per size.
# The app runs on X11 so that its window can be resized and read back from
# outside. X11 tools are required: xdotool and ImageMagick.
#
# Usage: snapshot.sh <ls_paint binary> <output directory>

set -euo pipefail

app=${1:?usage: snapshot.sh <ls_paint binary> <output directory>}
out_dir=${2:?usage: snapshot.sh <ls_paint binary> <output directory>}

for tool in xdotool import identify; do
    command -v "$tool" > /dev/null || { echo "Missing tool: $tool" >&2; exit 1; }
done

export DISPLAY=${DISPLAY:-:0}
# The capture tools below only see X11 windows, so the app must not use Wayland.
unset WAYLAND_DISPLAY

# The app itself refuses to go below 620px wide, and a window taller than the
# screen gets clamped by the window manager, so the matrix stays inside both.
sizes=(
    1600x960 1440x900 1280x800 1100x800 1024x768 960x720 900x700
    840x700 800x700 760x700 700x700 660x700 620x700
)

mkdir -p "$out_dir"

"$app" &
app_pid=$!
trap 'kill "$app_pid" 2> /dev/null || true' EXIT

# The window appears a moment after the process starts.
window=""
for _ in $(seq 1 50); do
    window=$(xdotool search --onlyvisible --pid "$app_pid" 2> /dev/null | head -1 || true)
    [ -n "$window" ] && break
    sleep 0.2
done
[ -n "$window" ] || { echo "No window appeared for pid $app_pid" >&2; exit 1; }

failures=0
for size in "${sizes[@]}"; do
    file="$out_dir/window-$size.png"
    xdotool windowmove "$window" 0 0
    xdotool windowsize "$window" "${size%x*}" "${size#*x}"
    # A resize request is asynchronous: wait until the window reports the new
    # size, then give the app a moment to redraw at it before reading pixels.
    for _ in $(seq 1 30); do
        eval "$(xdotool getwindowgeometry --shell "$window")"
        [ "${WIDTH}x${HEIGHT}" = "$size" ] && break
        sleep 0.1
    done
    sleep 0.3
    import -window "$window" "$file"
    actual=$(identify -format '%wx%h' "$file")
    if [ "$actual" != "$size" ]; then
        echo "Wrong size for $file: $actual" >&2
        failures=$((failures + 1))
        continue
    fi
    echo "Wrote $file"
done

echo "${#sizes[@]} screenshots in $out_dir"
[ "$failures" -eq 0 ]
