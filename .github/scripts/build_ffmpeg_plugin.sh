#!/bin/bash



#!/bin/bash
# filepath: /home/labrat/lgrab/SVT-JPEG-XS/ffmpeg-plugin/build_ffmpeg_svtjpegxs.sh

set -e

# Usage: ./build_ffmpeg_svtjpegxs.sh <ffmpeg-version: 6.1|7.0>
JPEGXS_REPO=$(pwd)
FFMPEG_VERSION=${1:-6.1}
INSTALL_FMPEG=${2:-"n"}



if [[ "$FFMPEG_VERSION" != "6.1" && "$FFMPEG_VERSION" != "7.0" ]]; then
    echo "Usage: $0  <ffmpeg-version> <install-ffmpeg>"
    echo "ffmpeg-version: 6.1|7.0"
    echo "install-ffmpeg: y|n (default: n)"
    echo "Example: $0 6.1 y"
    exit 1
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
git clone https://git.ffmpeg.org/ffmpeg.git ffmpeg-$FFMPEG_VERSION
cd ffmpeg-$FFMPEG_VERSION
git checkout "release/$FFMPEG_VERSION"

echo "=== 4. Apply jpeg-xs plugin patches ==="
git config --global user.email "runner@github.com"
git config --global user.name "action-runner"
cp "$JPEGXS_REPO/ffmpeg-plugin/libsvtjpegxs"* libavcodec/
git am --whitespace=fix "$JPEGXS_REPO/ffmpeg-plugin/$FFMPEG_VERSION/"*.patch

echo "=== 5. Configure FFmpeg ==="
./configure --enable-libsvtjpegxs --prefix="$INSTALL_DIR" --enable-shared
echo "=== 6. Build and install FFmpeg ==="
make -j"$(nproc)"
if [[ "$INSTALL_FMPEG" == "y" ]]; then
    make install
else
    echo "=== 6.2 Skip FFmpeg installation ==="
fi

echo "=== Build complete! ==="
echo "FFmpeg binary is in $INSTALL_DIR/bin/"
