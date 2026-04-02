# Linux ffmpeg plugin

## Notice

FFmpeg is an open source project licensed under LGPL and GPL.
See https://www.ffmpeg.org/legal.html.
You are solely responsible for determining if your use of FFmpeg requires any
additional licenses.
Intel is not responsible for obtaining any such licenses, nor liable for any
licensing fees due, in connection with your use of FFmpeg.

## 0. Create installation directory and export env variable

```text
mkdir install-dir
export INSTALL_DIR=$PWD/install-dir
```

## 1. Compile and install svt-jpegxs

```text
cd <jpeg-xs-repo>/Build/linux
./build.sh install --prefix $INSTALL_DIR
```

## 2. Export installation location

```text
export LD_LIBRARY_PATH="$INSTALL_DIR/lib:${LD_LIBRARY_PATH}"
export PKG_CONFIG_PATH="$INSTALL_DIR/lib/pkgconfig:${PKG_CONFIG_PATH}"
```

## 3. Download/Compile Ffmpeg

### a) Clone repository

```text
git clone https://git.ffmpeg.org/ffmpeg.git ffmpeg
cd ffmpeg
```

### b) Checkout to branch/tag (6.1, 7.0, 7.1, 8.0, 8.1)

```text
git checkout release/(6.1, 7.0, 7.1, 8.0, 8.1)
```

### c) copy files - ONLY for ffmpeg 6.0, 7.0, 7.1, 8.0

```text
cp <jpeg-xs-repo>/ffmpeg-plugin/libsvtjpegxs* libavcodec/
```

### d) apply jpeg-xs plugin patches

```text
git am --whitespace=fix <jpeg-xs-repo>/ffmpeg-plugin/(6.1, 7.0, 7.1, 8.0, 8.1)/*.patch
```

### e) Configure

```text
./configure --enable-libsvtjpegxs --prefix=$INSTALL_DIR --enable-shared
```

### f) build and install

```text
make -j40
make install
```

Note: for ffmpeg 8.0, 7.1, 7.0, or 6.1 version, replace 8.1 with the
corresponding version in the above commands.

## 4. Test executable

Binary (executable) is located in the main ffmpeg directory or
```$INSTALL_DIR/bin/```.

# Windows ffmpeg plugin

## 1. Download and install binary/installer from: https://www.msys2.org/

## 2. Open terminal MINGW64

<installation_path>\msys64\mingw64.exe
If you encounter an error about lack of support for the MSYS environment,
ensure MINGW64 is used.
You can switch to it from any shell by ```source shell mingw64```.
Your selected shell is written after the machine name,
for example ```user@user-mobl MSYS ~```.

## 3.  Configure new environment for MINGW64

### a) Export proxy(optional, if required)

```text
export ftp_proxy=<ftp>
export http_proxy=<http>
export https_proxy=<https>
```

### b) Install packages

```text
pacman -S make mingw-w64-x86_64-gcc mingw-w64-x86_64-cmake mingw-w64-x86_64-yasm mingw-w64-x86_64-diffutils
```

## 4. Create installation directory and export env variable

```text
mkdir install-dir
export INSTALL_DIR=$PWD/install-dir
```

## 5. Compile and install svt-jpeg-xs libs(In main svt-jpegxs folder)

### a) Configure

```text
cmake -S . -B svtjpegxs-build -DBUILD_APPS=off -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF -DCMAKE_INSTALL_PREFIX=$INSTALL_DIR
```

### b) Build

```text
cmake --build svtjpegxs-build -j10 --config Release --target install
```

## 6. Download/Compile Ffmpeg

### a) Clone repository

```text
git clone https://git.ffmpeg.org/ffmpeg.git ffmpeg
cd ffmpeg
```

### b) checkout to branch/tag (6.1, 7.0, 7.1, 8.0, 8.1)

```text
git checkout release/(6.1, 7.0, 7.1, 8.0, 8.1)
```

### c) copy files - ONLY for ffmpeg 6.0, 7.0, 7.1, 8.0

```text
cp <jpeg-xs-repo>/ffmpeg-plugin/libsvtjpegxs* libavcodec/
```

### d) apply plugin patches

```text
git am --whitespace=fix <jpeg-xs-repo>/ffmpeg-plugin/(6.1, 7.0, 7.1, 8.0, 8.1)/*.patch
```

### e) Export path for svt-jpeg-xs installation directory

```text
export PKG_CONFIG_PATH="$INSTALL_DIR/lib/pkgconfig:${PKG_CONFIG_PATH}"
```

### f) Configure Release Build

```text
./configure --enable-libsvtjpegxs --prefix=$INSTALL_DIR --enable-static --disable-shared
```

If a ```SvtJpegxs >= X.X.X not found using pkg-config``` error happens during
FFmpeg configuration, ensure ```$INSTALL_DIR``` points to a path with the
following folder-file structure:

```text
install-dir
    include
        svt-jpegxs
            SvtJpegxs.h
            SvtJpegxsDec.h
            SvtJpegxsEnc.h
            SvtJpegxsImageBufferTools.h
    lib
        libSvtJpegxs.a
        pkgconfig
            SvtJpegxs.pc
```

### g) build

```text
make -j10
```

Note: for ffmpeg 8.0, 7.1, 7.0, or 6.1 version, replace 8.1 with the
corresponding version in the above commands.

# How to use ffmpeg with jpeg-xs

## libsvtjpegxs encoder available params

Name|mandatory/optional|Accepted values|description
--|--|--|--
bpp|mandatory|any integer/float greater than 0 (example: 0.5, 3, 3.75, 5 etc.)|Bits Per Pixel
decomp_v|optional|0, 1, 2(default)|Number of Vertical decompositions
decomp_h|optional|0, 1, 2, 3, 4, 5(default)|Number of Horizontal decompositions, have to be greater or equal to decomp_v
threads|optional|Any integer in range< 1;64>|Number of threads encoder can create
slice_height|optional|(default:16), Any integer in range <1;source_height>, also it have to be multiple of 2^(decomp_v))|Coding feature: Specify slice height in units of picture luma pixels
quantization|optional|(default:deadzone), 0(deadzone), 1(uniform)|Coding feature: Quantization method
coding-signs|optional|(default:off), 0(off), 1(fast), 2(full)|Coding feature: Sign handling strategy
coding-sigf|optional|(default:on), 0(off), 1(on)|Coding feature: Significance coding
coding-vpred|optional|(default:off), 0(off), 1(on)|Coding feature: Vertical-prediction

## libsvtjpegxs decoder available params

Name|mandatory/optional|Accepted values|description
--|--|--|--
threads|optional|Any integer in range< 1;64>|Number of threads decoder can create
proxy-mode|optional|(default:full), 0(full), 1(half), 2(quarter)|Specify resolution scaling mode

### Encoding raw video

```text
./ffmpeg.exe -y -s:v 1920x1080 -c:v rawvideo -pix_fmt yuv420p -i <raw_stream.yuv> -codec jpegxs -bpp 1.25 <more encoder params -threads 5> encoded_file.mov
```

### Playback encoded stream via ffplay

```text
./ffplay.exe encoded_file.mov -threads 4
```

Bitstream can be stored in .mkv, .mp4, .mov containers

### Decoding jpegxs streams to raw video

```text
./ffmpeg.exe -threads 10 -i <jpegxs-file.mov> <output.yuv>
```

### Transcoding from any format to jpegxs

```text
./ffmpeg.exe -i <input.mov/.mp4/.mkv> -c:v jpegxs -bpp 2 -threads 15 encoder.mov
```
