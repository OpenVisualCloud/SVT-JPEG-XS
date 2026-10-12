# Intel&reg; JPEG-XS Library

[![OpenSSF Scorecard](https://api.scorecard.dev/projects/github.com/OpenVisualCloud/SVT-JPEG-XS/badge)](https://scorecard.dev/viewer/?uri=github.com/OpenVisualCloud/SVT-JPEG-XS)

## License

Scalable Video Technology is licensed under the OSI-approved BSD+Patent license. See [LICENSE](LICENSE.md) for details.

## Notice

You are solely responsible for determining if your use of jpeg-xs requires any additional licenses.
Intel is not responsible for obtaining any such licenses, nor liable for any licensing fees due, in connection with your use of jpeg-xs.

## Overview

This library is implementation of ISO/IEC 21122 protocol.

## Environment and Requirements

Encoder/Decoder sample app and libraries requirements:

- Any CPU that support x86-64 instruction set

Unit Tests requirements (SvtJpegxsUnitTests):

- Any CPU that support x86-64 and AVX2 instruction set

Supported OS versions:

- Linux Ubuntu 20.04 and 22.04
- Windows 10 and Windows 11

## Build and Install

### Windows* Operating Systems (64-bit)

- __Build Requirements__
  - Visual Studio* 2022 or 2019 (older version were not tested)
  - CMake 3.16 or later
  - YASM Assembler version 1.2.0 or later
    - Download the yasm executable from [https://yasm.tortall.net/Download.html](https://yasm.tortall.net/Download.html)
    - Rename `yasm-*-win64.exe` to `yasm.exe`
    - Copy `yasm.exe` into a location that is in the `PATH` environment variable (for example: `%USERPROFILE%\AppData\Local\Microsoft\WindowsApps\`)

- __Build Instructions__
  - Build the project by following the steps below
    - using terminal, cd into `<repo dir>\Build\windows`
    - run `build.bat <2022|2019>`, which will generate the Solution files (`*.sln`) and build the project

- __Binaries and libraries Location__
  - Binaries can be found under `<repo dir>/Bin/Release` or `<repo dir>/Bin/Debug`, depending on whether `Debug` or `Release` were selected in the build mode.

- __API headers location__
  - API headers can be found under `Source/API`

### Linux* Operating Systems (64-bit)

- __Build Requirements__
  - GCC 9.4.0 or later
  - CMake 3.16 or later
  - YASM Assembler version 1.2.0 or later

- __Build Instructions__
  - `cd Build/linux`
  - `./build.sh <release | debug>`

- __Build options__
  - Link-Time Optimization (LTO) is on by default for Release builds; disable with `./build.sh release --lto=off`
  - Profile-Guided Optimization (PGO) is off by default; enable with `./build.sh release --pgo=on` (Release builds only, requires the apps to be built to train against)
    - Training data: seed stills committed under `Build/linux/PGO/` (one single frame per
      resolution/bit-depth/colour-format combination) are each repeated to a short multi-frame clip,
      encoded, and the encoder's own output is immediately decoded - this self-contained
      encode-then-decode pass needs nothing beyond a plain `git clone` and is what every build trains on
    - If a `test_bitsreams/` conformance corpus is present under the samples root passed to RunPGO, an
      additional decode-only pass also runs over it for extra profile/level/format coverage the seed
      stills don't reach - this is a separate, optional addition, skipped gracefully (no error) when
      that corpus isn't available

- __Binaries and libraries location__
  - Binaries can be found under `Bin/Release` and/or `Bin/Debug`

- __API headers location__
  - API headers can be found under `Source/API`

## Encoder

### Supported colour formats

#### PLANAR

| format | EncApp: format + input-depth | ffmpeg name | status |
| -- | -- | -- | -- |
| YUV 420 8-bit | yuv420 + 8 | yuv420p | Tested, working properly |
| YUV 420 10/12/14-bit | yuv420 + 10/12/14 | yuv420p{10/12/14}le | Tested, working properly |
| YUV 422 8-bit | yuv422 + 8 | yuv422p | Tested, working properly |
| YUV 422 10/12/14-bit | yuv422 + 10/12/14 | yuv422p{10/12/14}le | Tested, working properly |
| YUV 444 8-bit | yuv444 + 8 | yuv444p | Tested, working properly. `--color-transform 1` is rejected, see "Component order and colour transform" below |
| YUV 444 10/12/14-bit | yuv444 + 10/12/14 | yuv444p{10/12/14}le | Tested, working properly. `--color-transform 1` is rejected, see "Component order and colour transform" below |
| RGB 8-bit | rgb  + 8 | gbrp (planes G, B, R, see below) | Tested, working properly. Planes in R, G, B order. Supports `--color-transform 1` (forward RCT, requires `--profile latency`) |
| RGB 10/12/14-bit | rgb + 10/12/14 | gbrp{10/12/14}le (planes G, B, R, see below) | Tested, working properly. Planes in R, G, B order. Supports `--color-transform 1` (forward RCT, requires `--profile latency`) |
| YUV 422 + Alpha (4:2:2:4) 8-bit | yuva422 + 8 | yuva422p | Tested, working properly |
| YUV 422 + Alpha (4:2:2:4) 10/12/14-bit | yuva422 + 10/12/14 | yuva422p{10/12}le (no 14-bit) | Tested, working properly |
| RGBA/YUVA444 (4:4:4:4) 8-bit | rgba/yuva444 + 8 | gbrap (planes G, B, R, A) / yuva444p | Tested, working properly. rgba planes in R, G, B, A order |
| RGBA/YUVA444 (4:4:4:4) 10/12/14-bit | rgba/yuva444 + 10/12/14 | gbrap{10/12/14}le (planes G, B, R, A) / yuva444p{10/12}le | Tested, working properly. rgba planes in R, G, B, A order |
| YUV 400 8-bit | yuv400 + 8 | - | Unsupported |
| YUV 400 10-bit | yuv400 + 10/12/14 | - | Unsupported |

#### PACKED

| format | EncApp: format + input-depth | ffmpeg name | status |
| -- | -- | -- | -- |
| RGB 8bit | rgbp + 8 | rgb24 (bytes in R, G, B order; bgr24 is not supported) | Tested, working properly, __decoder decodes to planar format only__, __`--profile cpu` (CPU_PROFILE_CPU) not implemented for this format, use the default Low latency profile__ |
| RGB 10/12/14bit | rgbp + 10/12/14 | - | Tested, working properly, __decoder decodes to planar format only__, __`--profile cpu` (CPU_PROFILE_CPU) not implemented for this format, use the default Low latency profile__ |

#### Component order and colour transform

The input file holds each frame as planes (planar formats) or interleaved bytes (`rgbp`), in the
order below. Components are coded in that order. The colour transform (`--color-transform 1`, RCT,
ISO/IEC 21122-1 Table F.2, signalled as Cpih=1) is defined for R, G, B input, so it is accepted
only for `rgb`; decoders treat Cpih=1 output as R, G, B.

| `--colour-format` | Library `colour_format` | Components per frame, in file order | FFmpeg plugin pixel format | `--color-transform 1` |
| -- | -- | -- | -- | -- |
| yuv400 (unsupported) | `COLOUR_FORMAT_PLANAR_YUV400` | Y | gray* (decode only) | rejected |
| yuv420 | `COLOUR_FORMAT_PLANAR_YUV420` | Y, Cb, Cr | yuv420p* | rejected |
| yuv422 | `COLOUR_FORMAT_PLANAR_YUV422` | Y, Cb, Cr | yuv422p* | rejected |
| yuv444 | `COLOUR_FORMAT_PLANAR_YUV444` | Y, Cb, Cr | yuv444p* | rejected |
| rgb | `COLOUR_FORMAT_PLANAR_RGB` | R, G, B | gbrp* (planes G, B, R; the plugin reorders them) | accepted |
| rgbp | `COLOUR_FORMAT_PACKED_YUV444_OR_RGB` | R, G, B interleaved per pixel | rgb24 | rejected |
| yuva422 | `COLOUR_FORMAT_PLANAR_YUV422_ALPHA` | Y, Cb, Cr, A | yuva422p* | rejected |
| yuva444 | `COLOUR_FORMAT_PLANAR_4_COMPONENTS` | Y, Cb, Cr, A | yuva444p* | rejected |
| rgba | `COLOUR_FORMAT_PLANAR_RGBA` | R, G, B, A | gbrap* (planes G, B, R, A; the plugin reorders them) | rejected (3 components only) |

Notes:

- FFmpeg has no planar pixel format in R, G, B order. A raw file written by FFmpeg as `gbrp*` or
  `gbrap*` holds G, B, R(, A) and must be reordered before it is passed to SvtJpegxsEncApp.
- `bgr24` (B, G, R bytes) is not supported. Convert it to `rgb24` first.
- `yuv444` is rejected with `--color-transform 1`: YUV input is already decorrelated, and the
  stream would be decoded as RGB.
- Without `--color-transform` (Cpih=0) the stream does not signal whether it holds YUV or RGB;
  `rgb` and `yuv444` produce identical streams. Only Cpih=1 streams are known to be RGB.

### Usage examples

#### Windows

```batch
SvtJpegxsEncApp.exe -i <input_file.yuv> -b <output_bitstream.bin> -w 1920 -h 1080 --input-depth 8 --colour-format yuv422 --bpp 5 --decomp_v 2 --decomp_h 5 --lp 4
```

#### Linux

```shell
./SvtJpegxsEncApp -i <input_file.yuv> -b <output_bitstream.bin> -w 1920 -h 1080 --input-depth 8 --colour-format yuv422 --bpp 5 --decomp_v 2 --decomp_h 5 --lp 4
```

#### Compression ratio

To encode stream with compression rate of 12:1 (assuming 8bit yuv420p), one pixel require 12bits so `--bpp 1` should be used:

```shell
./SvtJpegxsEncApp -i <input_file.yuv> -b <output_bitstream.bin> -w 1920 -h 1080 --input-depth 8 --colour-format yuv420 --bpp 1 --decomp_v 2 --decomp_h 5 --lp 4
```

#### Lossless coding

To encode a mathematically lossless stream (decoded output is bit-exact with the input), use
`--lossless 1`. `--bpp` is not required and is ignored:

```shell
./SvtJpegxsEncApp -i <input_file.yuv> -b <output_bitstream.bin> -w 1920 -h 1080 --input-depth 10 --colour-format yuv422 --lossless 1 --coding-raw 0
```

- The picture header signals Fq=0 and Bw equal to the input bit depth (ISO/IEC 21122-1 Table A.8,
  "lossless coding"), and the capability marker signals that lossless decoding support is required.
- Frame size is data-dependent: each frame is encoded into a worst-case sized buffer and then
  compacted, so compressible content produces smaller frames. `--bpp`, `--rc` and `--quantization`
  have no effect.
- Unless overridden with `--stream-profile`/`--stream-level`, the stream is declared as the MLS.12
  profile (Ppih=0x6EC0, ISO/IEC 21122-2 Table A.4) with an unrestricted sublevel. MLS.12 requires
  raw-mode to be disabled (Rl=0), so pass `--coding-raw 0` for a strictly conformant stream. MLS.12
  only covers 8/10/12-bit input; 13/14-bit lossless input is supported, but the encoder warns that
  no defined profile matches it.
- `--color-transform 1` (requires `--colour-format rgb` and `--profile latency`) can be combined with `--lossless 1`, since
  RCT is reversible, for 8-12-bit input only; at 13/14 bits the colour-transformed coefficients
  would exceed the coded magnitude range, so this combination is rejected.
- Not supported together with `--input-msb-aligned 1` or `--packetization-mode 1`; encoder
  initialization fails for these combinations.

#### Latency measurement

To measure average frame encoding time (latency) `--limit-fps`  and high enough `--lp` parameters have to be used, cmd example:

```shell
./SvtJpegxsEncApp -i <input_file.yuv> -b <output_bitstream.bin> -w 1920 -h 1080 --input-depth 8 --colour-format yuv422 --bpp 5 --decomp_v 2 --decomp_h 5 --lp 4 --limit-fps 60
```

The `--limit-fps` must be lower than the maximum achievable number of frames.
So that the next frames are not queued (time of waiting frames for processing is included in the latency time).

#### Interlaced video

To encode an interlaced stream, please provide a height that is half of the original.
For example, for a 1080i stream, use ```-w 1920 -h 540``` instead of ```-w 1920 -h 1080```

### Available parameters

Options:

```text
[--help]                   Show usage options and exit
[--show-bands]             Print detailed informations about bands
                            (enabled:1, disable:0, default:0)
[-v]                       Verbose Encoder internal level (disabled:0, errors:1,
                            system info:2, system info full:3, warnings:4, ... all:6,
                            default: 2)
```

Input Options:

```text
-i                         Input Filename
-w                         Frame width
-h                         Frame height
--colour-format            Set encoder colour format and the component order of the input file:
                            yuv420, yuv422, yuv444 (planes Y, Cb, Cr),
                            rgb (planes R, G, B), rgbp (packed, bytes R, G, B per pixel),
                            yuva444 (planes Y, Cb, Cr, A), rgba (planes R, G, B, A),
                            yuva422 (4:2:2:4, planes Y, Cb, Cr, A)
                            (Experimental: yuv400)
                            FFmpeg gbrp/gbrap files hold planes G, B, R(, A): reorder them for rgb/rgba
                            Only rgb is compatible with --color-transform
--input-depth              Input depth
[--input-msb-aligned]      Non-standard: 10/12-bit input samples are MSB-aligned in each 16-bit
                            word instead of LSB-aligned (enabled:1, disabled:0, default:0)
--bpp                      Bits Per Pixel, can be passed as integer or float
                            (example: 0.5, 3, 3.75, 5 etc.). Required unless --lossless 1 is set
[-n]                       Number of frames to encode
[--limit-fps]              Limit number of frames per second
                            (disabled: 0, enabled [1-240])
```

Output Options:

```text
[-b]                       Output filename
[--no-progress]            Do not print out progress (no print:1, print:0, default:0)
[--packetization-mode]     Specify how encoded stream is returned (multiple packets per frame:1, single packet per frame:0, default:0),
                           for details please refer to Codestream/Slice packetization mode
                           https://datatracker.ietf.org/doc/html/rfc9134
```

Coding features used during Rate Calculation (quality/speed tradeoff):

```text
[--decomp_v]               Vertical decomposition (0, 1, 2, default: 2)
[--decomp_h]               Horizontal decomposition have to be greater or equal to
                            decomp_v (1, 2, 3, 4, 5, default: 5)
[--quantization]           Quantization method(deadzone:0, uniform:1, default:0)
[--slice-height]           The height of each slice in units of picture luma pixels (default:16, any value that is multiple of 2^(decomp_v))
[--coding-signs]           Enable Signs handling strategy (full:2, fast:1, disable:0, default:0)
[--coding-sigf]            Enable signification coding (enabled:1, disable:0, default:1)
[--coding-vpred]           Enable Vertical Prediction coding (disable:0, zero prediction residuals:1, zero coefficients:2, default: 0)
[--coding-raw]             Enable packet-based raw-mode coding (enabled:1, disable for legacy-decoder compatibility:0, default:1)
[--cap-compat]             Emit an empty CAP marker for legacy-decoder compatibility when no capability bit is required (enabled:1, disable:0, default:0)
[--color-transform]        Encoder-side reversible colour transform (RCT, Cpih=1) (enabled:1,
                            disabled:0, default:0). Requires --colour-format rgb (planes R, G, B)
                            and --profile latency; rejected for every other format. Decoders
                            output R, G, B for Cpih=1 streams
[--lossless]               True lossless coding, Fq=0 (enabled:1, disabled:0, default:0). Output
                            size is data-dependent instead of the fixed CBR size. Ignores --bpp,
                            --rc and --quantization. Not supported together with
                            --input-msb-aligned 1, --packetization-mode 1, or --color-transform 1
                            at --input-depth 13/14. Use --coding-raw 0 for a strictly
                            MLS.12-conformant stream. See "Lossless coding" above
[--stream-profile]         Stream profile (Ppih) to declare in the picture header (auto, light422,
                            light444, lightsubline422, main420, main422, main444, main4444, high420,
                            high444, high4444, or raw hex/decimal Ppih value, default:auto)
[--stream-level]           Stream level (Plev) to declare in the picture header (auto, unrestricted,
                            1k-1, 2k-1, 4k-1, 4k-2, 4k-3, 5k-1, 8k-1, 8k-2, 8k-3, 10k-1, or raw
                            hex/decimal Plev value, default:auto)
[--rc]                     Rate Control mode (
                            CBR: budget per precinct: 0,
                            CBR: budget per precinct with padding movement: 1,
                            CBR: budget per slice: 2,
                            CBR: budget per slice with nax size RATE: 3,
                            default 1)
```

Threading, performance:

```text
[--asm]                    Limit assembly instruction set [0 - 11] or [c, mmx, sse, sse2,
                            sse3, ssse3, sse4_1, sse4_2, avx, avx2, avx512, max], by default
                            highest level supported by CPU
[--profile]                Threading model type, can be passed as digit (0, 1) or string
                            (latency, cpu) 0:latency (Low Latency mode), 1:cpu (Low CPU use
                            mode), default is 0. --color-transform requires latency (0);
                            rejected under cpu (1)
[--lp]                     Thread Scaling parameter, the higher the value the more threads
                            are created and thus lower latency and/or higher FPS can be
                            achieved (default: 0, which means lowest possible number of threads is created)
```

## Decoder

### Usage examples

#### Windows

```batch
SvtJpegxsDecApp.exe -i <input_bitstream.bin>  -o <output_file.yuv> --lp 5
```

#### Linux

```shell
./SvtJpegxsDecApp -i <input_bitstream.bin>  -o <output_file.yuv> --lp 5
```

#### Latency measurement

To measure average frame decoding time (latency) `--limit-fps`  and high enough `--lp` parameters have to be used, cmd example:

```shell
./SvtJpegxsDecApp -i <input_bitstream.bin>  -o <output_file.yuv> --lp 5 --limit-fps 60
```

The `--limit-fps` must be lower than the maximum achievable number of frames.
So that the next frames are not queued (time of waiting frames for processing is included in the latency time).

### Output format

SvtJpegxsDecApp writes each frame as planes, one component after another, in the order the stream
codes them. It prints the colour format reported by the library (`DecoderColorFormat`):

- Streams with the colour transform (Cpih=1) are reported as `COLOUR_FORMAT_PLANAR_RGB`
  (`COLOUR_FORMAT_PLANAR_RGBA` for 4 components); the planes are R, G, B(, A).
- 3-component 4:4:4 streams without it (Cpih=0) are reported as `COLOUR_FORMAT_PLANAR_YUV444`
  and 4-component 4:4:4:4 streams as `COLOUR_FORMAT_PLANAR_4_COMPONENTS`. A Cpih=0 stream does not
  signal whether it holds YUV or RGB: the planes are in the order the encoder received them
  (Y, Cb, Cr(, A) for YUV input, R, G, B(, A) for `rgb`/`rgba` input).
- Star-Tetrix streams (Cpih=3, 4 components) are reported as `COLOUR_FORMAT_PLANAR_4_COMPONENTS`.
- Streams encoded from packed `rgbp` input are decoded to planar R, G, B.

### Available parameters

Options:

```text
[--help]                   Show usage options and exit
[-v]                       Verbose decoder internal level (disabled:0, errors:1,
                            system info:2, system info full:3, warnings:4, ... all:6,
                            default: 2)
[--force-decode]           Force decode first frame
```

Input Options:

```text
-i                         Input Filename
[-n]                       Number of frames to decode
[--find-bitstream-header]  Find bitstream header
[--limit-fps]              Limit number of frames per second
                            (disabled: 0, enabled [1-240])
[--packetization-mode]     Specify how bitstream is passed to decoder
                            (multiple packets per frame:1, single packet per frame:0, default:0)
[--proxy-mode]             Resolution scaling mode(disabled: 0, scale 1/2: 1, scale 1/4: 2, default: 0)
[--output-msb-aligned]     Non-standard: 10/12-bit output samples are MSB-aligned in each 16-bit
                            word instead of LSB-aligned (enabled:1, disabled:0, default:0)
```

`--output-msb-aligned` is not applied to streams whose colour transform is done on the whole frame (for example
Star-Tetrix, Cpih=3); they are always output LSB-aligned. Streams with the reversible colour transform (RCT, Cpih=1) from
the SVT-JPEGXS encoder support it, and their inverse RCT is done per precinct, so it scales with `--lp`. See
[Final Stage](documentation/decoder/svt-jpegxs-decoder-design.md#final-stage).

Output Options:

```text
[-o]                       Output Filename
```

Threading, performance:

```text
[--asm]                    Limit assembly instruction set [0 - 11] or [c, mmx, sse, sse2, sse3,
                            ssse3, sse4_1, sse4_2, avx, avx2, avx512, max], by default highest
                            level supported by CPU
[--lp]                     Thread Scaling parameter, the higher the value the more threads are
                            created and thus lower latency and/or higher FPS can be achieved
                            (default: 0, which means lowest possible number of threads is created)
```

Decoder Proxy-mode limitation:

|   sampling    | decomp_v | proxy-mode 1/2 support | proxy-mode 1/4 support |
|       --      |    --    |           --           |           --           |
| YUV444/YUV422 |    2     |           Y            |           Y            |
| YUV444/YUV422 |    1     |           Y            |           N            |
| YUV444/YUV422 |    0     |           N            |           N            |
|    YUV420     |    2     |           Y            |           N            |
|    YUV420     |    1     |           N            |           N            |

### Thread wake-up

On Linux the decoder threads wait in short timed slices before blocking, which keeps the CPU cores out of deep idle states
and lowers the wake-up latency between pipeline stages. It uses POSIX calls only, so it works with any Linux C library. It
is enabled by default, applies to decoder sessions only, and can be changed with environment variables:

- `SVT_JXS_TW_US` - time budget in microseconds (default `1000`; `0`, a negative or an invalid value disables the timed
  wait).
- `SVT_JXS_TW_SLICE_US` - length of one timed slice in microseconds (default `50`; `0`, a negative or an invalid value
  keeps the default).
- `SVT_JXS_TW_SLACK_NS` - timer slack in nanoseconds set on the library threads (default `1000`; `0` means `1`, a negative
  or an invalid value keeps the default).

Values must be plain decimal integers. Values above one second are clamped to one second.

Hosts that already keep the cores out of deep idle states, for example low-latency setups for Media Transport Library /
SMPTE ST 2110 that hold `/dev/cpu_dma_latency` low, get no gain from the timed wait and only spend some extra CPU time. Set
`SVT_JXS_TW_US=0` there if CPU time is scarce.

```shell
SVT_JXS_TW_US=0 ./SvtJpegxsDecApp -i <input_bitstream.bin>  -o <output_file.yuv> --lp 5
```

Please see [Thread wake-up (timed wait)](documentation/decoder/svt-jpegxs-decoder-design.md#thread-wake-up-timed-wait) for
details and guidance on choosing the values.

## Encoder and Decoder design

Please see [Encoder design](documentation/encoder/svt-jpegxs-encoder-design.md)

Please see [Decoder design](documentation/decoder/svt-jpegxs-decoder-design.md)

## API usage

Please see [Encoder snippet](documentation/encoder/EncoderSnippets.md) for encoder structure overview and simplified encoder usage.

Please see [Decoder snippet](documentation/decoder/DecoderSnippets.md) for decoder structure overview and simplified decoder usage.

## CI/CD

Please see [CI/CD](documentation/ci-cd/README.md) for an overview of the GitHub Actions workflows
and how the core library build is shared across them.

## Logging

By default, library log messages (errors, warnings, info) are printed to `stderr`. This can be
controlled with two environment variables:

- `SVT_LOG` - minimum log level to print: `-1`(all), `0`(fatal), `1`(error), `2`(warn), `3`(info,
  default), `4`(debug). Out-of-range values are ignored and the default is kept.
- `SVT_LOG_FILE` - path to a file to write log messages to instead of `stderr`.

Host applications can also register a callback to intercept all log messages (e.g. to route them
into an application-specific logger) via `svt_jpeg_xs_set_log_callback()`, declared in
[Source/API/SvtJpegxs.h](Source/API/SvtJpegxs.h). This is a global, process-wide setting shared by
all encoder/decoder instances, and must be called before the first log call happens (i.e. before any
encoder/decoder init) to take effect.

## Notes

The information in this document was compiled at `v0.10` of the code and may not
reflect the latest status of the design. For the most up-to-date
settings and implementation, it's recommended to visit the specific section of the code.
