#!/usr/bin/env bash
# Builds LS Paint in Release mode and packs it as an AppImage.
#
#   ./build_appimage.sh
#   VERSION=1.2.3 JOBS=8 ./build_appimage.sh
#   ARCH=aarch64 ./build_appimage.sh
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")"
ROOT="$PWD"

VERSION="${VERSION:-0.1.1}"
JOBS="${JOBS:-$(nproc 2>/dev/null || echo 4)}"
BUILD_DIR="${BUILD_DIR:-build-appimage}"
OUT_DIR="${OUT_DIR:-dist}"
TOOLS_DIR="${TOOLS_DIR:-tools}"
ARCH="${ARCH:-$(uname -m)}"
inside_root() {
    local resolved
    resolved="$(realpath -m -- "$1")"
    [[ -n "$ROOT" && "$ROOT" != "/" && "$ROOT" != "${HOME:-}" ]] || return 1
    [[ "$resolved" == "$ROOT"/?* ]]
}

for name in BUILD_DIR OUT_DIR TOOLS_DIR; do
    value="${!name}"
    if [[ -z "$value" ]] || ! inside_root "$ROOT/$value"; then
        echo "error: $name='$value' must be a non-empty path inside $ROOT" >&2
        exit 1
    fi
done

APPDIR="$(realpath -m -- "$ROOT/$BUILD_DIR")/AppDir"
inside_root "$APPDIR" || { echo "error: refusing to use AppDir '$APPDIR'" >&2; exit 1; }

need() {
    command -v "$1" >/dev/null 2>&1 || { echo "error: '$1' not found. $2" >&2; exit 1; }
}

need cmake "Install the build dependencies listed in README.md."
need file "Install file (sudo apt install file)."

# --- Fetch linuxdeploy ------------------------------------------------------
LINUXDEPLOY_URL="https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-${ARCH}.AppImage"
PLUGIN_URL="https://github.com/linuxdeploy/linuxdeploy-plugin-appimage/releases/download/continuous/linuxdeploy-plugin-appimage-${ARCH}.AppImage"

fetch() { # url dest
    if command -v curl >/dev/null 2>&1; then
        curl -fL --retry 3 -o "$2" "$1"
    elif command -v wget >/dev/null 2>&1; then
        wget -O "$2" "$1"
    else
        echo "error: need curl or wget to download $1" >&2
        exit 1
    fi
}

mkdir -p "$TOOLS_DIR"
if [[ -z "${LINUXDEPLOY:-}" ]]; then
    LINUXDEPLOY="$TOOLS_DIR/linuxdeploy-${ARCH}.AppImage"
    if [[ ! -x "$LINUXDEPLOY" ]]; then
        echo "==> Downloading linuxdeploy"
        fetch "$LINUXDEPLOY_URL" "$LINUXDEPLOY"
        chmod +x "$LINUXDEPLOY"
    fi
    # linuxdeploy finds its plugins next to itself or on PATH.
    PLUGIN="$TOOLS_DIR/linuxdeploy-plugin-appimage-${ARCH}.AppImage"
    if [[ ! -x "$PLUGIN" ]]; then
        echo "==> Downloading linuxdeploy AppImage plugin"
        fetch "$PLUGIN_URL" "$PLUGIN"
        chmod +x "$PLUGIN"
    fi
    # The plugin is looked up as `linuxdeploy-plugin-appimage` (no suffix).
    ln -sf "$(basename "$PLUGIN")" "$TOOLS_DIR/linuxdeploy-plugin-appimage"
    export PATH="$ROOT/$TOOLS_DIR:$PATH"
fi

# Containers and CI often have no FUSE, so run the tool AppImages by extraction.
export APPIMAGE_EXTRACT_AND_RUN=1

# --- Build and install into the AppDir ---------------------------------------
echo "==> Configuring ($BUILD_DIR)"
cmake -S . -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/usr \
    -DCMAKE_INSTALL_LIBDIR=lib \
    -DBUILD_TESTING=OFF

echo "==> Building with $JOBS jobs"
cmake --build "$BUILD_DIR" -j "$JOBS"

echo "==> Installing into AppDir"
[[ "$(basename -- "$APPDIR")" == "AppDir" ]] && inside_root "$APPDIR" || { echo "error: refusing to delete '$APPDIR'" >&2; exit 1; }
rm -rf -- "$APPDIR"
DESTDIR="$APPDIR" cmake --install "$BUILD_DIR" --strip

# The desktop file must carry the same name as the icon and the binary.
DESKTOP="$APPDIR/usr/share/applications/ls_paint.desktop"
ICON="$APPDIR/usr/share/icons/hicolor/256x256/apps/ls_paint.png"
[[ -f "$DESKTOP" && -f "$ICON" ]] || { echo "error: install step missed the desktop file or icon." >&2; exit 1; }
if command -v desktop-file-validate >/dev/null 2>&1; then
    desktop-file-validate "$DESKTOP" || echo "warning: desktop file has validation issues."
fi

# --- Pack ---------------------------------------------------------------------
mkdir -p "$OUT_DIR"
rm -f -- "$ROOT/$OUT_DIR"/LS_Paint-*-"$ARCH".AppImage

echo "==> Bundling libraries and creating the AppImage"
export ARCH
export LINUXDEPLOY_OUTPUT_VERSION="$VERSION"
export LDAI_OUTPUT="$ROOT/$OUT_DIR/LS_Paint-${VERSION}-${ARCH}.AppImage"
# Libraries in this app's own prefix (libclip and anything Slint installed)
# are not on the loader's default path while linuxdeploy inspects the binary.
export LD_LIBRARY_PATH="$APPDIR/usr/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

"$LINUXDEPLOY" \
    --appdir "$APPDIR" \
    --executable "$APPDIR/usr/bin/ls_paint" \
    --desktop-file "$DESKTOP" \
    --icon-file "$ICON" \
    --output appimage

echo
echo "Done: $OUT_DIR/LS_Paint-${VERSION}-${ARCH}.AppImage"
ls -lh "$OUT_DIR"/LS_Paint-"${VERSION}"-"${ARCH}".AppImage
