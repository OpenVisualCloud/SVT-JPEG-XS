#!/usr/bin/env bash

set -e

JPEGXS_REPO="${PWD}"
FFMPEG_VERSION="${1:-6.1}"
COPY_FILES="${2:-y}"

if [[ "$FFMPEG_VERSION" != "6.1" && "$FFMPEG_VERSION" != "7.0" && "$FFMPEG_VERSION" != "7.1" && "$FFMPEG_VERSION" != "8.0" && "$FFMPEG_VERSION" != "8.1" && "$FFMPEG_VERSION" != "9.0" ]]; then
    echo "Usage: $0  <ffmpeg-version: 6.1|7.0|7.1|8.0|8.1|9.0> <copy-files: y|n>"
    echo "Example: $0 6.1 y"
    exit 1
fi

if [[ "$COPY_FILES" != "y" && "$COPY_FILES" != "n" ]]; then
    echo "Usage: $0  <ffmpeg-version: 6.1|7.0|7.1|8.0|8.1|9.0> <copy-files: y|n>"
    echo "Example: $0 6.1 y"
    exit 1
fi

if [[ ("$FFMPEG_VERSION" == "8.1" || "$FFMPEG_VERSION" == "9.0") && "$COPY_FILES" == "y" ]]; then
    echo "ffmpeg $FFMPEG_VERSION already ships libsvtjpegxs* upstream, forcing copy-files=n"
    COPY_FILES="n"
fi

pacman -S --noconfirm make mingw-w64-x86_64-gcc mingw-w64-x86_64-cmake mingw-w64-x86_64-yasm mingw-w64-x86_64-diffutils mingw-w64-x86_64-winpthreads mingw-w64-x86_64-toolchain
INSTALL_DIR="$PWD/install-dir"
mkdir -p "$INSTALL_DIR"

# 1. Compile and install svt-jpegxs
cd "$JPEGXS_REPO"
if find "$INSTALL_DIR" -name 'SvtJpegxs.pc' 2>/dev/null | grep -q .; then
    echo "svt-jpegxs already installed under $INSTALL_DIR (reused build artifact), skipping rebuild."
else
    ./Build/linux/build.sh install --prefix "$INSTALL_DIR" --no-app --static
fi

# 2. Set PKG_CONFIG_PATH
SVT_PC_FILE=$(find "$INSTALL_DIR" -name 'SvtJpegxs.pc' | head -1)
if [ -z "$SVT_PC_FILE" ]; then
    echo "FATAL: SvtJpegxs.pc not found under $INSTALL_DIR" >&2
    exit 1
fi
SVT_LIBDIR=$(dirname "$(dirname "$SVT_PC_FILE")")
export LD_LIBRARY_PATH="$SVT_LIBDIR:${LD_LIBRARY_PATH}"
export PKG_CONFIG_PATH="$SVT_LIBDIR/pkgconfig:${PKG_CONFIG_PATH}"

# 3. Download/Compile FFmpeg
cd "$PWD"
git config --global user.email "runner@github.com"
git config --global user.name "action-runner"

if [[ -n "$FFMPEG_SRC_DIR" && -d "$FFMPEG_SRC_DIR/.git" ]]; then
    echo "Reusing pre-fetched FFmpeg source from $FFMPEG_SRC_DIR"
    cp -r "$FFMPEG_SRC_DIR" "ffmpeg-${FFMPEG_VERSION}"
    cd "ffmpeg-$FFMPEG_VERSION"
    git checkout "release/$FFMPEG_VERSION"
else
    clone_attempt=1
    max_clone_attempts=5
    until git clone --branch "release/$FFMPEG_VERSION" --depth 1 https://github.com/FFmpeg/FFmpeg.git "ffmpeg-${FFMPEG_VERSION}"; do
        if [[ "$clone_attempt" -ge "$max_clone_attempts" ]]; then
            echo "git clone failed after $max_clone_attempts attempts, giving up."
            exit 1
        fi
        clone_attempt=$((clone_attempt + 1))
        echo "git clone failed (attempt $((clone_attempt - 1))/$max_clone_attempts), retrying in 10s..."
        rm -rf "ffmpeg-${FFMPEG_VERSION}"
        sleep 10
    done
    cd "ffmpeg-$FFMPEG_VERSION"
fi

# 4. Apply jpeg-xs plugin patches
if [[ "$COPY_FILES" == "y" ]]; then
    cp "$JPEGXS_REPO/ffmpeg-plugin/libsvtjpegxs"* libavcodec/
fi
git am --whitespace=fix "$JPEGXS_REPO/ffmpeg-plugin/$FFMPEG_VERSION/"*.patch

# 5. Configure FFmpeg
./configure --enable-libsvtjpegxs --prefix="$INSTALL_DIR" --enable-static --disable-shared

# 6. Build FFmpeg
make -j10

echo "Build complete! FFmpeg binary is in $INSTALL_DIR/bin/"
