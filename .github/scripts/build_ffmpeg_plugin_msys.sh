#!/usr/bin/env bash

set -e

JPEGXS_REPO="${PWD}"
FFMPEG_VERSION="${1:-6.1}"

if [[ "$FFMPEG_VERSION" != "6.1" && "$FFMPEG_VERSION" != "7.0" ]]; then
    echo "Usage: $0  <ffmpeg-version: 6.1|7.0> (default: 6.1)"
    exit 1
fi
pacman -S --noconfirm make mingw-w64-x86_64-gcc mingw-w64-x86_64-cmake mingw-w64-x86_64-yasm mingw-w64-x86_64-diffutils mingw-w64-x86_64-winpthreads mingw-w64-x86_64-toolchain
INSTALL_DIR="$PWD/install-dir"
mkdir -p "$INSTALL_DIR"

# 1. Compile and install svt-jpegxs
cd "$JPEGXS_REPO"
cmake -S . -B svtjpegxs-build -DBUILD_APPS=off -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR"
cmake --build svtjpegxs-build -j10 --config Release --target install

# 2. Set PKG_CONFIG_PATH
export LD_LIBRARY_PATH="$INSTALL_DIR/lib:${LD_LIBRARY_PATH}"
export PKG_CONFIG_PATH="$INSTALL_DIR/lib/pkgconfig:${PKG_CONFIG_PATH}"

# 3. Download/Compile FFmpeg
cd "$PWD"
git config --global user.email "runner@github.com"
git config --global user.name "action-runner"

git clone https://git.ffmpeg.org/ffmpeg.git "ffmpeg-$FFMPEG_VERSION"
cd "ffmpeg-$FFMPEG_VERSION"
git checkout "release/$FFMPEG_VERSION"

# 4. Apply jpeg-xs plugin patches
cp "$JPEGXS_REPO/ffmpeg-plugin/libsvtjpegxs"* libavcodec/
git am --whitespace=fix "$JPEGXS_REPO/ffmpeg-plugin/$FFMPEG_VERSION/"*.patch

# 5. Configure FFmpeg
./configure --enable-libsvtjpegxs --prefix="$INSTALL_DIR" --enable-static --disable-shared

# 6. Build FFmpeg
make -j10

echo "Build complete! FFmpeg binary is in $INSTALL_DIR/bin/"
