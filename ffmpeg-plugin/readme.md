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

### c) copy files - ONLY for ffmpeg 6.1, 7.0, 7.1, 8.0

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
pacman -S make mingw-w64-x86_64-gcc mingw-w64-x86_64-cmake mingw-w64-x86_64-yasm diffutils mingw-w64-x86_64-winpthreads mingw-w64-x86_64-toolchain
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

### c) copy files - ONLY for ffmpeg 6.1, 7.0, 7.1, 8.0

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
  to the version lists in the usage comment and usage messages, and to the `COPY_FILES`
  auto-disable check if the new version ships `libsvtjpegxs*` natively upstream (like 8.1/9.0 do -
  no `cp .../libsvtjpegxs*` needed for those).
- `.github/workflows/ffmpeg_plugin_build.yaml`:
  - `ffmpeg-source-fetch` job: add the new version to the `for v in 6.1 7.0 7.1 8.0 8.1 9.0; do ...`
    loop - this is the stage that clones/fetches and checks out each supported FFmpeg release
    branch once, shared by every build job (see
    [documentation/ci-cd/README.md](../documentation/ci-cd/README.md)). Also update the
    hard-coded count in that step's name (`Fetch: all 6 FFmpeg release branches ...`).
  - Add a new `ffmpeg-X-Y-linux-build` job (copy an existing one, update the version passed to
    `build_ffmpeg_plugin.sh`, which takes `<version> <install: y|n> <copy-files: y|n>` and picks
    the patches from `ffmpeg-plugin/<version>/`) and its matching `ffmpeg-X-Y-linux-tests` job.
  - Add the new `ffmpeg-X-Y-linux-tests` job to the `needs` list of the
    `ffmpeg-9-0-performance-tests` job.
  - Add a new `ffmpeg-X-Y-windows-build` job (copy an existing one, update the version passed to
    `build_ffmpeg_plugin_msys.sh`).
  - Add new `LINUX_ARTIFACTS_X_Y`/`WINDOWS_ARTIFACTS_X_Y` entries to the workflow's `env:` block.
  - If the new version becomes the latest one, move the `ffmpeg-9-0-performance-tests` job (and
    its `needs`/artifact references) to point at the new version instead.
- `ffmpeg-plugin/<X.Y>/`: add the new version's patch directory (native plugin patches, following
  the 8.1/9.0 pattern) or `libsvtjpegxs*` copy-file pattern (6.1-8.0 pattern).
- `tests/scripts/FFmpeg*.sh` and `tests/scripts/PerformanceTestFfmpegPlugin.sh`: update if the new
  version needs different test coverage or performance baselines.
- `documentation/ci-cd/README.md`: update the version list and count in the workflow description
  table, and the per-version job counts (e.g. "all 6 `ffmpeg-*-windows-build` jobs").

# How to use ffmpeg with jpeg-xs

## Supported pixel formats

Name|Bit depths|Notes
--|--|--
yuv420p(le)|8, 10, 12, 14|-
yuv422p(le)|8, 10, 12, 14|-
yuv444p(le), gbrp(le)|8, 10, 12, 14|gbrp planes are passed to the library in R, G, B order. Decoder output depends on the `rgb` option, see "RGB formats" below
gray(le), gray16le|8, 9, 10, 12, 14, 16|decode only. gray16le is used for bit depths above 14. Other bit depths, see "Decoder output for other bit depths" below
rgb24|8|packed, encode only. Rejected with `-cpu_profile cpu`. bgr24 is not supported: ffmpeg converts it to a supported format
yuva422p(le)|8, 10, 12|4:2:2:4 (YUV422 + alpha)
yuva444p(le), gbrap(le)|8, 10, 12, 14|4:4:4:4 (RGBA/YUVA444), gbrap planes are passed to the library in R, G, B, A order. Decoder output depends on the `rgb` option, see "RGB formats" below. No upstream ffmpeg `yuva444p14`/`yuva422p14` pix_fmt exists, so 14-bit 4:4:4:4 is only reachable via `gbrap14le`, and 14-bit 4:2:2:4 is not reachable through this plugin at all.

### Decoder output for other bit depths

The decoder library accepts streams with any bit depth from 8 to 16, but the plugin only has
correctly labelled output formats for the bit depths in the table above. For other bit depths the
decoded samples are not rescaled: they are written unchanged into the next output format listed
below, so the values do not use the full range that the output format declares.

- 4:2:0, 4:2:2 and 4:4:4 (3 components), 9, 11, 13, 15 or 16 bit: output as the matching `*p14le`
  format (`yuv420p14le`, `yuv422p14le`, `yuv444p14le` or `gbrp14le`). 15/16-bit samples do not fit
  the declared 14-bit range.
- 4:4:4:4 (4 components), 9 or 11 bit: output as `gbrap14le` (planes in stream order unless the
  stream is decoded as RGB, see "RGB formats"). 13-bit 4-component streams are also output as
  `gbrap14le`, holding 13-bit values.
- 4:2:2:4, any bit depth other than 8, 10 or 12: rejected.
- gray: 9-bit maps to `gray9le`. 11-bit maps to `gray12le` for ffmpeg 6.1, 7.0, 7.1, 8.0 and to
  `gray14le` for ffmpeg 8.1, 9.0. 13-bit maps to `gray14le` and 15-bit to `gray16le`.

Only 8, 10, 12 and 14-bit streams (and 9 and 16-bit gray) are labelled correctly.

## RGB formats

The library codes components in the order it receives them. RGB input must reach it as R, G, B(, A)
(`COLOUR_FORMAT_PLANAR_RGB`/`COLOUR_FORMAT_PLANAR_RGBA`), which is also the order the colour
transform (`color_transform`, RCT, Cpih=1) expects. FFmpeg `gbrp*`/`gbrap*` frames hold the planes
as G, B, R(, A), so the plugin reorders the plane pointers in both directions (no copy).

### Encoding

Input pixel format|Coded components, in order|color_transform
--|--|--
yuv444p*|Y, Cb, Cr|rejected
gbrp*|R, G, B (planes reordered)|accepted
rgb24|R, G, B|rejected (packed input)
yuva444p*|Y, Cb, Cr, A|rejected
gbrap*|R, G, B, A (planes reordered)|rejected (3 components only)
yuv420p*, yuv422p*, yuva422p*|Y, Cb, Cr(, A)|rejected

`bgr24` is not accepted: ffmpeg converts it to a supported format before encoding.

### Decoding

A stream with the colour transform (Cpih=1) is known to hold R, G, B(, A). A stream without it
(Cpih=0) does not signal whether it holds YUV or RGB, so the output depends on the decoder `rgb`
option. Pass decoder options before `-i`.

Stream|rgb auto (default)|rgb true|rgb false
--|--|--|--
3 components 4:4:4, Cpih=1|gbrp*|gbrp*|yuv444p*, planes R, G, B in stream order
3 components 4:4:4, Cpih=0|yuv444p*, planes in stream order|gbrp* (components read as R, G, B)|yuv444p*, planes in stream order
4 components 4:4:4:4, 8-12 bit, Cpih=1|gbrap*|gbrap*|yuva444p*, planes R, G, B, A in stream order
4 components 4:4:4:4, 8-12 bit, Cpih=0 or Cpih=3|yuva444p*, planes in stream order|gbrap* (components read as R, G, B, A)|yuva444p*, planes in stream order
4 components 4:4:4:4, 13/14 bit, any Cpih|gbrap14le (components read as R, G, B, A)|gbrap14le (components read as R, G, B, A)|gbrap14le, planes in stream order
4:2:0, 4:2:2, 4:2:2:4, gray|not affected|not affected|not affected

13/14-bit 4-component streams always decode to `gbrap14le` because no upstream `yuva444p14`
exists. They are read as R, G, B, A by default, which matches the encoder's `gbrap14le` input; use
`-rgb false` for YUVA or Star-Tetrix (Cpih=3) streams to keep the stream order.

Typical use:

- Stream encoded from `gbrp*` with `-color_transform 1`: decode with default options.
- Stream encoded from `gbrp*`, `rgb24` or 8-12-bit `gbrap*` without the colour transform: decode
  with `-rgb true`.
- Stream encoded from `gbrap14le`: decode with default options.
- Stream encoded from `yuv444p*` or `yuva444p*`: decode with default options.

Streams encoded from `gbrp*`, `gbrap*` or `bgr24` by earlier plugin versions store the components
in the wrong order (G, B, R(, A) or B, G, R) and decode with wrong colours. Decode them with
`-rgb false` and relabel the planes without converting sample values (set the matching bit depth in
`format`):

- `gbrp*` (stored G, B, R):

  ```text
  ./ffmpeg -rgb false -i <old_gbrp_stream> -vf "mergeplanes=map0s=0:map0p=0:map1s=0:map1p=1:map2s=0:map2p=2:format=gbrp" <output>
  ```

- 8-12-bit `gbrap*` (stored G, B, R, A):

  ```text
  ./ffmpeg -rgb false -i <old_gbrap_stream> -vf "mergeplanes=map0s=0:map0p=0:map1s=0:map1p=1:map2s=0:map2p=2:map3s=0:map3p=3:format=gbrap" <output>
  ```

- `gbrap14le` (stored G, B, R, A): `-rgb false` alone gives the right output, no relabelling needed.
- `bgr24` (stored B, G, R):

  ```text
  ./ffmpeg -rgb false -i <old_bgr24_stream> -vf "mergeplanes=map0s=0:map0p=1:map1s=0:map1p=0:map2s=0:map2p=2:format=gbrp" <output>
  ```

## libsvtjpegxs encoder available params

Name|mandatory/optional|Accepted values|description
--|--|--|--
bpp|optional|any integer/float greater than 0 (example: 0.5, 3, 3.75, 5 etc.)|Bits Per Pixel. If not set, defaults to an uncompressed-equivalent value based on the input pixel format (a warning is logged with the auto-selected value)
decomp_v|optional|0, 1, 2(default)|Number of Vertical decompositions. 0 is rejected for yuv420p* input
decomp_h|optional|0, 1, 2, 3, 4, 5(default)|Number of Horizontal decompositions, have to be greater or equal to decomp_v
threads|optional|Any integer in range< 1;64>|Number of threads encoder can create
slice_height|optional|(default:16), Any integer in range <1;source_height>, also it have to be multiple of 2^(decomp_v))|Coding feature: Specify slice height in units of picture luma pixels
quantization|optional|(default:deadzone), 0(deadzone), 1(uniform)|Coding feature: Quantization method
coding-signs|optional|(default:disable), 0(disable), 1(fast), 2(full)|Coding feature: Sign handling strategy
coding-sigf|optional|(default:on), 0(off), 1(on)|Coding feature: Significance coding
coding-vpred|optional|(default:disable), 0(disable), 1(no_residuals), 2(no_coeffs)|Coding feature: Vertical-prediction
coding-raw|optional|(default:auto/enabled), true(enabled), false(disable for legacy-decoder compatibility)|Coding feature: packet-based raw-mode coding
cap-compat|optional|(default:auto/disabled), true(enabled), false(disabled)|Emit an empty CAP marker for legacy-decoder compatibility when no capability bit is required
color_transform|optional|(default:auto/disabled), true(enabled), false(disabled)|Encoder-side reversible colour transform (RCT, Cpih=1). Only valid for planar RGB input (gbrp, gbrp10le, gbrp12le, gbrp14le) with `cpu_profile` latency - rejected otherwise, including yuv444p: YUV input is already decorrelated, and Cpih=1 streams are decoded as RGB
cpu_profile|optional|(default:auto/latency), latency, cpu|Encoder internal threading model. `color_transform` and rgb24 input require latency
msb_aligned|optional|(default:false), true, false|Non-standard: input 10/12-bit samples are MSB-aligned in each 16-bit word instead of LSB-aligned. Must match the decoder's msb_aligned setting
lossless|optional|(default:false), true, false|True lossless coding (Fq=0): decoded output is bit-exact with the input. Output size is data-dependent instead of the usual fixed CBR size; `-bpp` is not required and is ignored, as is `quantization`. Unless `-profile:v`/`-level:v` are set, the stream is declared as MLS.12 (Ppih=0x6EC0) with an unrestricted sublevel; add `-coding-raw false` for a strictly MLS.12-conformant stream (MLS.12 requires Rl=0). Rejected together with `-msb_aligned true`, or with `-color_transform true` at 13/14-bit input

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
- Named level values only cover the resolution/level portion of Plev (bits [15:10]); use a raw
  hex/decimal value instead of a name to also set an explicit sublevel/FBB-level, or for a profile
  not in the named list.
- These names and values mirror the SvtJpegxsEncApp's `--stream-profile`/`--stream-level` CLI options
  (see the encoder "Available parameters" in [README.md](../README.md#available-parameters) and
  `Source/App/EncApp/EncAppConfig.c`) and `Source/Lib/Encoder/Codec/ProfileLevel.h`.
- Out-of-range values (outside 0-65535) are rejected with an error at encoder init.

## libsvtjpegxs decoder available params

Name|mandatory/optional|Accepted values|description
--|--|--|--
threads|optional|Any integer in range< 1;64>|Number of threads decoder can create
proxy-mode|optional|(default:full), 0(full), 1(half), 2(quarter)|Specify resolution scaling mode
msb_aligned|optional|(default:false), true, false|Non-standard: output 10/12-bit samples are MSB-aligned in each 16-bit word instead of LSB-aligned. Must match the encoder's msb_aligned setting
rgb|optional|(default:auto), auto, true, false|Output pixel format for 4:4:4 and 4:4:4:4 streams, see the decoding table in "RGB formats". auto: gbrp*/gbrap* for streams with the colour transform (Cpih=1), yuv444p*/yuva444p* otherwise (13/14-bit 4-component streams: always gbrap14le, read as R, G, B, A). true: always gbrp*/gbrap*; use for RGB streams coded without the colour transform. false: always yuv444p*/yuva444p* with the planes in stream order (13/14-bit 4-component streams: gbrap14le in stream order)

Note: decoder options (private ones such as `-msb_aligned`, and the generic ffmpeg `-threads`)
must be placed BEFORE `-i`, not after - placed after `-i` they are silently ignored (ffmpeg only
logs a warning, no error).

### Encoding raw video

```text
./ffmpeg.exe -y -s:v 1920x1080 -c:v rawvideo -pix_fmt yuv420p -i <raw_stream.yuv> -codec jpegxs -bpp 1.25 <more encoder params -threads 5> encoded_file.mov
```

### Encoding raw video losslessly

```text
./ffmpeg.exe -y -s:v 1920x1080 -c:v rawvideo -pix_fmt yuv422p10le -i <raw_stream.yuv> -codec jpegxs -lossless true -coding-raw false encoded_file.mov
```

### Playback encoded stream via ffplay

```text
./ffplay.exe encoded_file.mov -threads 4
```

Bitstream can be stored in .mkv, .mp4, .mov, .ts (MPEG-TS) containers.

MPEG-TS support differs between versions:

- ffmpeg 6.1, 7.0, 7.1, 8.0: only stream_type 0x32 is set, no JPEG XS descriptor is written into the
  PMT.
- ffmpeg 8.1, 9.0: the muxer also writes the JPEG XS video descriptor (`JXS_video_descriptor`) into
  the PMT, and the demuxer's parsing of the descriptor's `schar` field is fixed (4:2:0 subsampling
  and RGB detection).

### Decoding raw .jxs codestreams

ffmpeg 8.1 and 9.0 have the `jpegxs_pipe` demuxer (with a patched JPEG XS parser), which reads a
file holding one or more concatenated codestreams:

```text
./ffmpeg.exe -f jpegxs_pipe -c:v libsvtjpegxs -i <stream.jxs> -f rawvideo <output.yuv>
```

ffmpeg 6.1, 7.0, 7.1 and 8.0 have no `jpegxs_pipe` demuxer. Use `-f image2`, which reads only one
frame (codestream) per file:

```text
./ffmpeg.exe -f image2 -c:v libsvtjpegxs -i <single_frame.jxs> -f rawvideo <output.yuv>
```

### Decoding jpegxs streams to raw video

```text
./ffmpeg.exe -threads 10 -i <jpegxs-file.mov> <output.yuv>
```

### Transcoding from any format to jpegxs

```text
./ffmpeg.exe -i <input.mov/.mp4/.mkv> -c:v jpegxs -bpp 2 -threads 15 encoder.mov
```
