#!/usr/bin/env bash
# Builds LS Paint in Release mode and packs it as a .deb and/or .rpm with CPack.
#
#   ./build_packages.sh              build both packages
#   ./build_packages.sh deb          only the .deb
#   ./build_packages.sh rpm          only the .rpm
#   VERSION=1.2.3 JOBS=8 ./build_packages.sh
#
# Packages are written to ./dist. The .deb needs dpkg-dev, the .rpm needs rpmbuild.
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")"

VERSION="${VERSION:-0.1.1}"
JOBS="${JOBS:-$(nproc 2>/dev/null || echo 4)}"
BUILD_DIR="${BUILD_DIR:-build-pkg}"
OUT_DIR="${OUT_DIR:-dist}"
MAINTAINER="${MAINTAINER:-LS Paint <noreply@localhost>}"
SUMMARY="Microsoft Paint clone"

case "${1:-all}" in
    all) generators=(DEB RPM) ;;
    deb) generators=(DEB) ;;
    rpm) generators=(RPM) ;;
    *) echo "usage: $0 [deb|rpm]" >&2; exit 2 ;;
esac

need() {
    command -v "$1" >/dev/null 2>&1 || { echo "error: '$1' not found. $2" >&2; exit 1; }
}

need cmake "Install the build dependencies listed in README.md."
need cpack "It ships with cmake."
# Only the packages whose tools are installed get built; naming one explicitly
# makes a missing tool an error.
available=()
for generator in "${generators[@]}"; do
    case "$generator" in
        DEB) tool=dpkg-shlibdeps; hint="Install dpkg-dev (sudo apt install dpkg-dev)." ;;
        RPM) tool=rpmbuild; hint="Install rpm-build (sudo dnf install rpm-build, or sudo apt install rpm)." ;;
    esac
    if command -v "$tool" >/dev/null 2>&1; then
        available+=("$generator")
    elif [[ "${1:-all}" == all ]]; then
        echo "note: skipping $generator, '$tool' not found. $hint"
    else
        need "$tool" "$hint"
    fi
done
# CPack works out the .deb dependencies with file(1), which minimal images lack.
if [[ " ${available[*]} " == *" DEB "* ]]; then
    need file "Install file (sudo apt install file)."
fi
[[ ${#available[@]} -gt 0 ]] || { echo "error: nothing to build." >&2; exit 1; }
generators=("${available[@]}")

echo "==> Configuring ($BUILD_DIR)"
cmake -S . -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/usr \
    -DBUILD_TESTING=OFF

echo "==> Building with $JOBS jobs"
cmake --build "$BUILD_DIR" -j "$JOBS"

mkdir -p "$OUT_DIR"

# The installed binary finds its libraries through an $ORIGIN-relative rpath,
# which rpmbuild's rpath check rejects by default.
export QA_RPATHS=0x003f

for generator in "${generators[@]}"; do
    echo "==> Packing $generator"
    cpack --config "$BUILD_DIR/CPackConfig.cmake" -G "$generator" -B "$OUT_DIR" \
        -D CPACK_PACKAGE_NAME=ls-paint \
        -D CPACK_PACKAGE_VERSION="$VERSION" \
        -D CPACK_PACKAGE_CONTACT="$MAINTAINER" \
        -D CPACK_PACKAGE_VENDOR="LS Paint" \
        -D CPACK_PACKAGE_DESCRIPTION_SUMMARY="$SUMMARY" \
        -D CPACK_RESOURCE_FILE_LICENSE="$PWD/LICENSE" \
        -D CPACK_STRIP_FILES=ON \
        -D CPACK_PACKAGING_INSTALL_PREFIX=/usr \
        -D CPACK_DEBIAN_PACKAGE_SECTION=graphics \
        -D CPACK_DEBIAN_FILE_NAME=DEB-DEFAULT \
        -D CPACK_DEBIAN_PACKAGE_SHLIBDEPS=ON \
        -D CPACK_RPM_PACKAGE_LICENSE=GPLv3 \
        -D CPACK_RPM_PACKAGE_GROUP=Applications/Multimedia \
        -D CPACK_RPM_FILE_NAME=RPM-DEFAULT \
        -D CPACK_RPM_PACKAGE_AUTOREQ=ON \
        -D CPACK_RPM_EXCLUDE_FROM_AUTO_FILELIST_ADDITION="/usr;/usr/bin;/usr/lib;/usr/lib64;/usr/share;/usr/share/applications;/usr/share/icons;/usr/share/icons/hicolor;/usr/share/icons/hicolor/256x256;/usr/share/icons/hicolor/256x256/apps"
done

echo
echo "Done. Packages in $OUT_DIR/:"
ls -1 "$OUT_DIR"/*.deb "$OUT_DIR"/*.rpm 2>/dev/null || true
