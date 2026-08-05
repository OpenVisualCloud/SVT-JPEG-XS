#!/bin/bash
#
# Copyright(c) 2025 Intel Corporation
# SPDX - License - Identifier: BSD - 2 - Clause - Patent
#
# Builds SVT-JPEG-XS and a minimal GStreamer subset (core + gst-plugins-base
# essentials + the gst-plugins-bad "svtjpegxs" plugin only) to exercise the
# GStreamer svtjpegxsenc/svtjpegxsdec elements in CI.
#
# NOTE: as of writing, the svtjpegxs plugin is NOT present in any released
# GStreamer version (checked up to 1.28.5) - it only exists on the
# unreleased "main" branch. A pinned commit on "main" is used below instead
# of a release tag; re-pin GST_COMMIT once the plugin ships in a release.
#
# Usage: ./build_gstreamer_plugin.sh [gstreamer-commit]
#   1: git commit SHA on the GStreamer monorepo "main" branch to build
#      (optional, defaults to the pinned commit below)

set -e

JPEGXS_REPO=$(pwd)
GST_COMMIT=${1:-8ca913845a142ebbea86eede74292d840f12a046}

echo "=== 0. Create installation directory and export env variable ==="
export INSTALL_DIR="$JPEGXS_REPO/install-dir"
rm -rf "$INSTALL_DIR"
mkdir -p "$INSTALL_DIR"

echo "=== 1. Compile and install svt-jpegxs ==="
cd "$JPEGXS_REPO/Build/linux"
./build.sh install --prefix "$INSTALL_DIR"
cd "$JPEGXS_REPO"

echo "=== 2. Make the SvtJpegxs.pc pkg-config file discoverable ==="
# CMAKE_INSTALL_LIBDIR (via GNUInstallDirs) is "lib" on Debian/Ubuntu but
# "lib64" on Fedora/RHEL - don't assume, find the actual install location.
SVT_PC_FILE=$(find "$INSTALL_DIR" -name 'SvtJpegxs.pc' | head -1)
if [ -z "$SVT_PC_FILE" ]; then
    echo "FATAL: SvtJpegxs.pc not found under $INSTALL_DIR" >&2
    exit 1
fi
SVT_LIBDIR=$(dirname "$(dirname "$SVT_PC_FILE")")
export PKG_CONFIG_PATH="$SVT_LIBDIR/pkgconfig:${PKG_CONFIG_PATH:-}"
pkg-config --modversion SvtJpegxs

echo "=== 3. Ensure a new-enough Meson is available (repo's packaged one is usually too old) ==="
# Recent Debian/Ubuntu (PEP 668 "externally-managed-environment") refuse
# `pip install --user` outright. Use an isolated venv instead - works the
# same everywhere and doesn't touch system/user site-packages.
MESON_VENV="$JPEGXS_REPO/.meson-venv"
python3 -m venv "$MESON_VENV"
export PATH="$MESON_VENV/bin:$PATH"
python3 -m pip install --upgrade "meson>=1.4"
meson --version

echo "=== 4. Fetch GStreamer monorepo at the pinned commit (shallow, no full-history clone) ==="
rm -rf "$JPEGXS_REPO/gst-build"
mkdir -p "$JPEGXS_REPO/gst-build"
cd "$JPEGXS_REPO/gst-build"
git init -q
git fetch --depth 1 --no-tags https://gitlab.freedesktop.org/gstreamer/gstreamer.git "$GST_COMMIT"
git checkout -q FETCH_HEAD

echo "=== 5. Configure a minimal build: core + base essentials + svtjpegxs plugin only ==="
# -Dlibdir=lib pins a predictable install layout (some distros default to
# lib64/lib/x86_64-linux-gnu depending on multiarch detection).
meson setup build \
  -Dlibdir=lib \
  -Dauto_features=disabled -Dtools=enabled \
  -Dbase=enabled -Dgood=disabled -Dugly=disabled -Dbad=enabled \
  -Dlibav=disabled -Ddevtools=disabled -Dges=disabled -Drtsp_server=disabled \
  -Dgst-examples=disabled -Dpython=disabled -Dsharp=disabled -Dtls=disabled \
  -Dlibnice=disabled -Dgtk=disabled -Dgpl=disabled \
  -Dgst-plugins-base:rawparse=enabled -Dgst-plugins-base:videoconvertscale=enabled \
  -Dgst-plugins-base:app=enabled -Dgst-plugins-base:typefind=enabled \
  -Dgst-plugins-bad:svtjpegxs=enabled

echo "=== 6. Build ==="
ninja -C build

echo "=== 7. Install into a self-contained, relocatable tree (no meson/ninja needed to run it) ==="
GST_INSTALL_DIR="$JPEGXS_REPO/gst-install"
rm -rf "$GST_INSTALL_DIR"
DESTDIR="$GST_INSTALL_DIR" meson install -C build
GST_PREFIX="$GST_INSTALL_DIR/usr/local"

echo "=== 8. Write an env file describing how to use the two install trees ==="
cat > "$JPEGXS_REPO/gst-plugin-env.sh" <<EOF
# Source this file to run gst-launch-1.0/gst-inspect-1.0 against the
# svtjpegxs plugin built by build_gstreamer_plugin.sh.
export PATH="$GST_PREFIX/bin:\$PATH"
export LD_LIBRARY_PATH="$GST_PREFIX/lib:$SVT_LIBDIR:\${LD_LIBRARY_PATH:-}"
export GST_PLUGIN_PATH="$GST_PREFIX/lib/gstreamer-1.0"
EOF

echo "=== Build complete! ==="
echo "SVT-JPEG-XS install:  $INSTALL_DIR"
echo "GStreamer install:    $GST_INSTALL_DIR"
echo "Env file:             $JPEGXS_REPO/gst-plugin-env.sh (source it, then use gst-launch-1.0/gst-inspect-1.0 directly)"
