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

### b) Checkout to branch/tag (6.1, 7.0, 7.1, 8.0, 8.1, 9.0)

```text
git checkout release/(6.1, 7.0, 7.1, 8.0, 8.1, 9.0)
```

### c) copy files - ONLY for ffmpeg 6.0, 7.0, 7.1, 8.0

```text
cp <jpeg-xs-repo>/ffmpeg-plugin/libsvtjpegxs* libavcodec/
```

### d) apply jpeg-xs plugin patches

```text
git am --whitespace=fix <jpeg-xs-repo>/ffmpeg-plugin/(6.1, 7.0, 7.1, 8.0, 8.1, 9.0)/*.patch
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

### b) checkout to branch/tag (6.1, 7.0, 7.1, 8.0, 8.1, 9.0)

```text
git checkout release/(6.1, 7.0, 7.1, 8.0, 8.1, 9.0)
```

### c) copy files - ONLY for ffmpeg 6.0, 7.0, 7.1, 8.0

```text
cp <jpeg-xs-repo>/ffmpeg-plugin/libsvtjpegxs* libavcodec/
```

### d) apply plugin patches

```text
git am --whitespace=fix <jpeg-xs-repo>/ffmpeg-plugin/(6.1, 7.0, 7.1, 8.0, 8.1, 9.0)/*.patch
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

# Adding support for a new FFmpeg version

Every currently supported version (6.1, 7.0, 7.1, 8.0, 8.1, 9.0) is hardcoded in several places.
Adding a new one requires touching all of these:

- `.github/scripts/build_ffmpeg_plugin.sh` and `.github/scripts/build_ffmpeg_plugin_msys.sh`:
  add the new version to the `FFMPEG_VERSION` validation list (`if [[ "$FFMPEG_VERSION" != ... ]]`),
  and to the `COPY_FILES` auto-disable check if the new version ships `libsvtjpegxs*` natively
  upstream (like 8.1/9.0 do - no `cp .../libsvtjpegxs*` needed for those).
- `.github/workflows/ffmpeg_plugin_build.yaml`:
  - `ffmpeg-source-fetch` job: add the new version to the `for v in 6.1 7.0 7.1 8.0 8.1 9.0; do ...`
    loop - this is the stage that clones/fetches and checks out each supported FFmpeg release
    branch once, shared by every build job (see
    [documentation/ci-cd/README.md](../documentation/ci-cd/README.md)).
  - Add a new `ffmpeg-X-Y-linux-build` job (copy an existing one, update the version/patch args
    passed to `build_ffmpeg_plugin.sh`) and its matching `ffmpeg-X-Y-linux-tests` job.
  - Add a new `ffmpeg-X-Y-windows-build` job (copy an existing one, update the version passed to
    `build_ffmpeg_plugin_msys.sh`).
  - Add new `LINUX_ARTIFACTS_X_Y`/`WINDOWS_ARTIFACTS_X_Y` entries to the workflow's `env:` block.
  - If the new version becomes the latest one, move the `ffmpeg-9-0-performance-tests` job (and
    its `needs`/artifact references) to point at the new version instead.
- `ffmpeg-plugin/<X.Y>/`: add the new version's patch directory (native plugin patches, following
  the 8.1/9.0 pattern) or `libsvtjpegxs*` copy-file pattern (6.1-8.0 pattern).
- `tests/scripts/FFmpeg*.sh` and `tests/scripts/PerformanceTestFfmpegPlugin.sh`: update if the new
  version needs different test coverage or performance baselines.
- `documentation/ci-cd/README.md`: update the version list mentioned in the workflow description
  table.

# How to use ffmpeg with jpeg-xs

## Supported pixel formats

Name|Bit depths|Notes
--|--|--
yuv420p(le)|8, 10, 12, 14|-
yuv422p(le)|8, 10, 12, 14|-
yuv444p(le), gbrp(le)|8, 10, 12, 14|-
gray(le)|8, 9, 10, 12, 14|decode only
rgb24, bgr24|8|packed
yuva422p(le)|8, 10, 12|4:2:2:4 (YUV422 + alpha)
yuva444p(le), gbrap(le)|8, 10, 12, 14|4:4:4:4 (RGBA/GBRA/YUVA444). No upstream ffmpeg `yuva444p14`/`yuva422p14` pix_fmt exists, so 14-bit 4:4:4:4 is only reachable via `gbrap14le`, and 14-bit 4:2:2:4 is not reachable through this plugin at all.

## libsvtjpegxs encoder available params

Name|mandatory/optional|Accepted values|description
--|--|--|--
bpp|optional|any integer/float greater than 0 (example: 0.5, 3, 3.75, 5 etc.)|Bits Per Pixel. If not set, defaults to an uncompressed-equivalent value based on the input pixel format (a warning is logged with the auto-selected value)
decomp_v|optional|0, 1, 2(default)|Number of Vertical decompositions
decomp_h|optional|0, 1, 2, 3, 4, 5(default)|Number of Horizontal decompositions, have to be greater or equal to decomp_v
threads|optional|Any integer in range< 1;64>|Number of threads encoder can create
slice_height|optional|(default:16), Any integer in range <1;source_height>, also it have to be multiple of 2^(decomp_v))|Coding feature: Specify slice height in units of picture luma pixels
quantization|optional|(default:deadzone), 0(deadzone), 1(uniform)|Coding feature: Quantization method
coding-signs|optional|(default:off), 0(off), 1(fast), 2(full)|Coding feature: Sign handling strategy
coding-sigf|optional|(default:on), 0(off), 1(on)|Coding feature: Significance coding
coding-vpred|optional|(default:off), 0(off), 1(on)|Coding feature: Vertical-prediction
coding-raw|optional|(default:auto/enabled), true(enabled), false(disable for legacy-decoder compatibility)|Coding feature: packet-based raw-mode coding
cap-compat|optional|(default:auto/disabled), true(enabled), false(disabled)|Emit an empty CAP marker for legacy-decoder compatibility when no capability bit is required
msb_aligned|optional|(default:false), true, false|Non-standard: input 10/12-bit samples are MSB-aligned in each 16-bit word instead of LSB-aligned. Must match the decoder's msb_aligned setting

### Stream profile (Ppih) and level (Plev)

The picture header's profile (Ppih) and level (Plev) fields are normally derived automatically from
the input pixel format, resolution and `-bpp`. To override them, use ffmpeg's generic `-profile`/
`-level` options (these are not libsvtjpegxs-private options, they are the same options every ffmpeg
encoder exposes) - do not use a plugin-specific flag for this.

```text
./ffmpeg -y -i <input> -c:v libsvtjpegxs -bpp 8 -profile:v main444 -level:v 8k-1 out.mov
```

Use `-profile:v`/`-level:v` (with the `:v` stream specifier) rather than bare `-profile`/`-level`:
ffmpeg prints `-profile is ambiguous` and warns when the specifier is omitted, even though the value
is still applied correctly in a video-only pipeline.

Name|mandatory/optional|Accepted values|description
--|--|--|--
profile|optional|(default: unset/auto-derive), a name (light422, light444, lightsubline422, main420, main422, main444, main4444, high420, high444, high4444) or a raw 0-65535 value (e.g. 0x3540)|Override the auto-derived stream profile (Ppih) written into the picture header
level|optional|(default: unset/auto-derive), a name (unrestricted, 1k-1, 2k-1, 4k-1, 4k-2, 4k-3, 5k-1, 8k-1, 8k-2, 8k-3, 10k-1) or a raw 0-65535 value (e.g. 0x0810) to also set an explicit sublevel|Override the auto-derived stream level (Plev) written into the picture header

Notes:

- Leaving `-profile`/`-level` unset (the default) keeps the existing auto-derive behavior: the
  encoder always signals a validly-defined ISO/IEC 21122-2 Annex A codeword based on the actual
  encoder configuration.
- Named values only cover the "Main" profile family and the resolution/level portion of Plev (bits
  [15:10]); use a raw hex/decimal value instead of a name for Light/High family profiles or to also
  set an explicit sublevel/FBB-level.
- These names and values mirror the SvtJpegxsEncApp's `--stream-profile`/`--stream-level` CLI options
  (see [documentation/encoder/EncoderSnippets.md](../documentation/encoder/EncoderSnippets.md) and
  `Source/App/EncApp/EncAppConfig.c`) and `Source/Lib/Encoder/Codec/ProfileLevel.h`.
- Out-of-range values (outside 0-65535) are rejected with an error at encoder init.

## libsvtjpegxs decoder available params

Name|mandatory/optional|Accepted values|description
--|--|--|--
threads|optional|Any integer in range< 1;64>|Number of threads decoder can create
proxy-mode|optional|(default:full), 0(full), 1(half), 2(quarter)|Specify resolution scaling mode
msb_aligned|optional|(default:false), true, false|Non-standard: output 10/12-bit samples are MSB-aligned in each 16-bit word instead of LSB-aligned. Must match the encoder's msb_aligned setting

Note: private decoder options (e.g. `-msb_aligned`, `-threads`) must be placed BEFORE `-i`, not
after - placed after `-i` they are silently ignored (ffmpeg only logs a warning, no error).

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
