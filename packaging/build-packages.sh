#!/usr/bin/env bash
# Builds the NotePadLX release packages into dist/:
#   dist/notepadlx_<version>_amd64.deb        (CPack, dependencies from dpkg-shlibdeps)
#   dist/NotePadLX-<version>-x86_64.AppImage  (linuxdeploy + linuxdeploy-plugin-qt)
#
# Usage: packaging/build-packages.sh [deb|appimage|all]   (default: all)
# Needs: cmake, ninja, Qt 6 development packages, dpkg-dev; the AppImage tools are downloaded into
# build-release/tools on the first run.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="$ROOT/build-release"
DIST="$ROOT/dist"
TOOLS="$BUILD/tools"
WHAT="${1:-all}"

mkdir -p "$DIST" "$TOOLS"

echo "==> Configuring the release build"
cmake -S "$ROOT" -B "$BUILD" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/usr \
    -DNPLX_BUILD_TESTS=OFF \
    -DNPLX_USE_SOURCE_DATA_DIR=OFF
echo "==> Building"
cmake --build "$BUILD" --parallel "${JOBS:-4}"  # limited: a full -j build runs out of memory

VERSION="$(sed -n 's/^CMAKE_PROJECT_VERSION:STATIC=//p' "$BUILD/CMakeCache.txt")"

if [[ "$WHAT" == "deb" || "$WHAT" == "all" ]]; then
    echo "==> Building the .deb package"
    (cd "$BUILD" && cpack -G DEB -B "$DIST/cpack")
    mv -f "$DIST/cpack/"*.deb "$DIST/"
    rm -rf "$DIST/cpack"
    echo "    $(ls "$DIST"/*.deb)"
fi

if [[ "$WHAT" == "appimage" || "$WHAT" == "all" ]]; then
    echo "==> Building the AppImage"
    LINUXDEPLOY="$TOOLS/linuxdeploy-x86_64.AppImage"
    LINUXDEPLOY_QT="$TOOLS/linuxdeploy-plugin-qt-x86_64.AppImage"
    [[ -x "$LINUXDEPLOY" ]] || curl -fL -o "$LINUXDEPLOY" \
        https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage
    [[ -x "$LINUXDEPLOY_QT" ]] || curl -fL -o "$LINUXDEPLOY_QT" \
        https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-x86_64.AppImage
    chmod +x "$LINUXDEPLOY" "$LINUXDEPLOY_QT"

    APPDIR="$BUILD/AppDir"
    rm -rf "$APPDIR"
    DESTDIR="$APPDIR" cmake --install "$BUILD"

    # Qt: the SVG icons (image format plugin) and Wayland next to X11
    export QMAKE="${QMAKE:-$(command -v qmake6 || echo /usr/lib/qt6/bin/qmake6)}"
    export EXTRA_QT_MODULES="svg;waylandclient"
    export EXTRA_PLATFORM_PLUGINS="libqwayland.so"
    export APPIMAGE_EXTRACT_AND_RUN=1  # no FUSE needed to run the tools
    export LDAI_OUTPUT="$DIST/NotePadLX-$VERSION-x86_64.AppImage"
    export LINUXDEPLOY_OUTPUT_VERSION="$VERSION"
    export NO_STRIP=1  # linuxdeploy's bundled strip is too old for the system's binaries

    (cd "$BUILD" && "$LINUXDEPLOY" --appdir "$APPDIR" \
        --executable "$APPDIR/usr/bin/notepadlx" \
        --desktop-file "$APPDIR/usr/share/applications/notepadlx.desktop" \
        --icon-file "$ROOT/resources/icons/notepadlx-256.png" --icon-filename notepadlx \
        --plugin qt --output appimage)
    echo "    $LDAI_OUTPUT"
fi

echo "==> Done:"
ls -lh "$DIST"
