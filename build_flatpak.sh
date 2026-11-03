#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")"
ROOT="$PWD"

VERSION="${VERSION:-0.1.1}"
APP_ID="${APP_ID:-io.github.Bowserinator.LSPaint}"
RUNTIME_VERSION="${RUNTIME_VERSION:-25.08}"
LLVM_EXT="${LLVM_EXT:-llvm20}"
BUILD_DIR="${BUILD_DIR:-build-flatpak}"
OUT_DIR="${OUT_DIR:-dist}"
INSTALL="${INSTALL:-0}"
ARCH="$(uname -m)"

inside_root() {
    local resolved
    resolved="$(realpath -m -- "$1")"
    [[ -n "$ROOT" && "$ROOT" != "/" && "$ROOT" != "${HOME:-}" ]] || return 1
    [[ "$resolved" == "$ROOT"/?* ]]
}

for name in BUILD_DIR OUT_DIR; do
    value="${!name}"
    if [[ -z "$value" ]] || ! inside_root "$ROOT/$value"; then
        echo "error: $name='$value' must be a non-empty path inside $ROOT" >&2
        exit 1
    fi
done

need() {
    command -v "$1" >/dev/null 2>&1 || { echo "error: '$1' not found. $2" >&2; exit 1; }
}

need flatpak "Install flatpak (sudo dnf install flatpak)."
need flatpak-builder "Install flatpak-builder (sudo dnf install flatpak-builder)."

echo "==> Checking the Flathub remote"
flatpak remote-add --user --if-not-exists flathub https://dl.flathub.org/repo/flathub.flatpakrepo

WORK="$ROOT/$BUILD_DIR"
MANIFEST="$WORK/$APP_ID.yml"
mkdir -p "$WORK" "$ROOT/$OUT_DIR"

cat > "$MANIFEST" <<EOF
id: $APP_ID
runtime: org.freedesktop.Platform
runtime-version: "$RUNTIME_VERSION"
sdk: org.freedesktop.Sdk
sdk-extensions:
  - org.freedesktop.Sdk.Extension.rust-stable
  - org.freedesktop.Sdk.Extension.$LLVM_EXT
command: ls_paint
finish-args:
  - --device=dri
  - --share=ipc
  - --socket=wayland
  - --socket=fallback-x11
  - --filesystem=home
build-options:
  append-path: /usr/lib/sdk/rust-stable/bin:/usr/lib/sdk/$LLVM_EXT/bin
modules:
  - name: ls-paint
    buildsystem: cmake-ninja
    build-options:
      build-args:
        - --share=network
      env:
        CARGO_HOME: /run/build/ls-paint/cargo
        LIBCLANG_PATH: /usr/lib/sdk/$LLVM_EXT/lib
    config-opts:
      - -DCMAKE_BUILD_TYPE=Release
      - -DCMAKE_INSTALL_LIBDIR=lib
      - -DBUILD_TESTING=OFF
    post-install:
      - mv /app/share/applications/ls_paint.desktop /app/share/applications/\${FLATPAK_ID}.desktop
      - sed -i "s|^Icon=.*|Icon=\${FLATPAK_ID}|" /app/share/applications/\${FLATPAK_ID}.desktop
      - mv /app/share/icons/hicolor/256x256/apps/ls_paint.png /app/share/icons/hicolor/256x256/apps/\${FLATPAK_ID}.png
    sources:
      - type: dir
        path: $ROOT
        skip:
          - .git
          - $BUILD_DIR
          - $OUT_DIR
          - build
          - build-appimage
          - build-pkg
          - build-win
          - build-msvc
          - tools
EOF

BUNDLE="$ROOT/$OUT_DIR/LS_Paint-$VERSION-$ARCH.flatpak"
rm -f -- "$BUNDLE"

builder_args=(--user --force-clean --install-deps-from=flathub
    --state-dir="$WORK/state" --repo="$WORK/repo")
if [[ "$INSTALL" == "1" ]]; then
    builder_args+=(--install)
fi

echo "==> Building $APP_ID (the first build compiles Slint and downloads Skia)"
flatpak-builder "${builder_args[@]}" "$WORK/build" "$MANIFEST"

echo "==> Creating the bundle"
flatpak build-bundle "$WORK/repo" "$BUNDLE" "$APP_ID" \
    --runtime-repo=https://dl.flathub.org/repo/flathub.flatpakrepo

echo
echo "Done: $OUT_DIR/LS_Paint-$VERSION-$ARCH.flatpak"
echo "Install: flatpak install --user $OUT_DIR/LS_Paint-$VERSION-$ARCH.flatpak"
echo "Run:     flatpak run $APP_ID"
