#!/bin/bash

# filepath: /home/labrat/lgrab/SVT-JPEG-XS/ffmpeg-plugin/build_ffmpeg_svtjpegxs.sh

set -e

# Usage: ./build_ffmpeg_svtjpegxs.sh <ffmpeg-version: 6.1|7.0|7.1|8.0|8.1> <install_ffmpeg: y|n>
JPEGXS_REPO=$(pwd)
FFMPEG_VERSION=${1:-6.1}
INSTALL_FMPEG=${2:-"n"}
COPY_FILES=${3:-"y"}

if [[ "$FFMPEG_VERSION" != "6.1" && "$FFMPEG_VERSION" != "7.0" && "$FFMPEG_VERSION" != "7.1" && "$FFMPEG_VERSION" != "8.0" && "$FFMPEG_VERSION" != "8.1" && "$FFMPEG_VERSION" != "9.0" ]]; then
    echo "Usage: $0  <ffmpeg-version> <install-ffmpeg>"
    echo "ffmpeg-version: 6.1|7.0|7.1|8.0|8.1|9.0 (required)"
    echo "Example: $0 6.1 y"
    exit 1
fi

if [[ "$COPY_FILES" != "y" && "$COPY_FILES" != "n" ]]; then
    echo "Usage: $0  <ffmpeg-version: 6.1|7.0|7.1|8.0|8.1|9.0> <copy-files: y|n>"
    echo "Example: $0 6.1 y y"
    exit 1
fi

if [[ ("$FFMPEG_VERSION" == "8.1" || "$FFMPEG_VERSION" == "9.0") && "$COPY_FILES" == "y" ]]; then
    echo "ffmpeg $FFMPEG_VERSION already ships libsvtjpegxs* upstream, forcing copy-files=n"
    COPY_FILES="n"
fi

echo "=== 0. Create installation directory and export env variable ==="
export INSTALL_DIR="$PWD/install-dir"
mkdir -p "$INSTALL_DIR"

echo "=== 1. Compile and install svt-jpegxs ==="
cd "$JPEGXS_REPO/Build/linux"
./build.sh install --prefix "$INSTALL_DIR"

echo "=== 2. Export installation location ==="
export LD_LIBRARY_PATH="$INSTALL_DIR/lib:${LD_LIBRARY_PATH}"
export PKG_CONFIG_PATH="$INSTALL_DIR/lib/pkgconfig:${PKG_CONFIG_PATH}"

echo "=== 3. Download/Compile FFmpeg ==="
cd "$PWD"
# Shallow, branch-specific clone: avoids downloading the full ffmpeg git history (which is large
# and slow) since only one release branch is ever needed here.
# Retry on transient network/server errors (e.g. HTTP 502 from git.ffmpeg.org), which are common
# under load and not indicative of a real problem with the clone itself.
clone_attempt=1
max_clone_attempts=5
until git clone --branch "release/$FFMPEG_VERSION" --depth 1 https://git.ffmpeg.org/ffmpeg.git "ffmpeg-${FFMPEG_VERSION}"; do
    if [[ "$clone_attempt" -ge "$max_clone_attempts" ]]; then
        echo "git clone failed after $max_clone_attempts attempts, giving up."
        exit 1
    fi
    clone_attempt=$((clone_attempt + 1))
    echo "git clone failed (attempt $((clone_attempt - 1))/$max_clone_attempts), retrying in 10s..."
    rm -rf "ffmpeg-${FFMPEG_VERSION}"
    sleep 10
done
cd "ffmpeg-${FFMPEG_VERSION}"

echo "=== 4. Apply jpeg-xs plugin patches ==="
git config --global user.email "runner@github.com"
git config --global user.name "action-runner"
if [[ "$COPY_FILES" == "y" ]]; then
    cp "$JPEGXS_REPO/ffmpeg-plugin/libsvtjpegxs"* libavcodec/
fi
git am --whitespace=fix "$JPEGXS_REPO/ffmpeg-plugin/$FFMPEG_VERSION/"*.patch

echo "=== 5. Configure FFmpeg ==="
# --disable-avdevice: we only ever decode/encode via libavcodec+libavformat (rawvideo/image2/
# jpegxs_pipe/image2pipe); libavdevice's input/output devices (x11grab, alsa, sdl2, v4l2, ...) are
# unused here but get auto-detected against whatever optional system libs happen to be installed
# on the machine that RUNS `configure`/`make`. On a shared/dynamically-linked build, that produces
# an `ffmpeg` binary that dynamically links against those libs (e.g. libxcb.so.1) and then fails
# to start at all on a different machine (e.g. a self-hosted test runner) that doesn't have them
# installed - disabling avdevice entirely removes this whole class of cross-machine runtime
# dependency mismatches.
./configure --enable-libsvtjpegxs --prefix="$INSTALL_DIR" --enable-shared --disable-avdevice
echo "=== 6. Build and install FFmpeg ==="
make -j"$(nproc)"
if [[ "$INSTALL_FMPEG" == "y" ]]; then
    make install
else
    echo "=== 6.2 Skip FFmpeg installation ==="
fi

echo "=== Build complete! ==="
echo "FFmpeg binary is in $INSTALL_DIR/bin/"
