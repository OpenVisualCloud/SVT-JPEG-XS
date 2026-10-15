# SVT-JPEG-XS v0.10.0 Release Notes

Changes since v0.9.0.

## API changes

API version: 0.10 (`SVT_JPEGXS_API_VER_MINOR` raised from 9 to 10).

### Breaking changes

- **ABI break against v0.9.0.** New fields were added to
  `svt_jpeg_xs_encoder_api_t` and `svt_jpeg_xs_decoder_api_t`, and both
  structs now end with a `padding` array that reserves room for future
  fields. Applications built against v0.9.0 must be recompiled.
- **Separate 4:4:4 YUV and RGB colour formats.**
  `COLOUR_FORMAT_PLANAR_YUV444_OR_RGB` is replaced by
  `COLOUR_FORMAT_PLANAR_YUV444` and `COLOUR_FORMAT_PLANAR_RGB`.
  The old name is kept as a deprecated alias of
  `COLOUR_FORMAT_PLANAR_YUV444`.
  - The decoder reports streams that use the colour transform (Cpih=1) as
    `COLOUR_FORMAT_PLANAR_RGB` or `COLOUR_FORMAT_PLANAR_RGBA`, not as
    `COLOUR_FORMAT_PLANAR_YUV444_OR_RGB` or
    `COLOUR_FORMAT_PLANAR_4_COMPONENTS`.
  - `enable_color_transform` is rejected for every format except
    `COLOUR_FORMAT_PLANAR_RGB`.
  - RGB planes must be passed in R, G, B (A) order.
- `SvtJpegxsEncApp --colour-format yuv400` was removed. The encoder does
  not support single-component input. Single-component streams can only
  be decoded.

### New colour formats (`ColourFormat_t`)

- `COLOUR_FORMAT_PLANAR_YUV444`: planar YUV 4:4:4.
- `COLOUR_FORMAT_PLANAR_RGB`: planar RGB, R, G, B order.
- `COLOUR_FORMAT_PLANAR_RGBA`: planar RGB plus alpha.
- `COLOUR_FORMAT_PLANAR_YUV422_ALPHA`: 4:2:2:4, meaning planar YUV 4:2:2
  plus a full-resolution alpha plane.
- `COLOUR_FORMAT_PLANAR_4_COMPONENTS` is now documented and supported as
  4:4:4:4 (YUVA444, or RGBA without the colour transform).

### Encoder (`svt_jpeg_xs_encoder_api_t`)

| Field | Purpose |
| --- | --- |
| `lossless_enable` | True lossless coding (Fq=0). The decoded output is bit-exact with the input. |
| `enable_color_transform` | Forward reversible colour transform (RCT, Cpih=1) for planar RGB input. |
| `input_bit_depth_msb_aligned` | 10/12-bit input samples stored in the high bits of each 16-bit word. |
| `profile_ppih_override` | Overrides the profile (Ppih) that the encoder derives for the picture header. |
| `level_plev_override` | Overrides the level (Plev) that the encoder derives for the picture header. |
| `coding_raw_disable` | Disables raw-mode packet coding and clears its CAP bit. Helps with strict hardware decoders. |
| `cap_compat` | Writes an empty CAP marker when no capability bit is set. Helps with strict hardware decoders. |

### Decoder (`svt_jpeg_xs_decoder_api_t`)

| Field / function | Purpose |
| --- | --- |
| `proxy_mode` (`proxy_mode_t`) | Decodes at full, 1/2 or 1/4 width and height. |
| `output_bit_depth_msb_aligned` | Writes 10/12-bit output samples in the high bits of each 16-bit word. |
| `svt_jpeg_xs_decoder_get_single_frame_size_with_proxy()` | Returns the frame size for a given proxy mode. |

### Common (`SvtJpegxs.h`)

- `svt_jpeg_xs_set_log_callback()`, together with `SvtLogLevel` and
  `SvtJxsLogCallback`, sends all library log output to a callback that
  the application provides.
- New CPU flags `CPU_FLAGS_POPCNT`, `CPU_FLAGS_BMI2` and `CPU_FLAGS_NEON`.
  `CPU_FLAGS_ALL` now includes them.
- The API headers now document the image buffer, stride and bitstream
  buffer fields, the component order of each colour format and the
  frame pool release rules.

## Features

### Codec

- **Lossless coding.** True lossless (Fq=0) mode, enabled with
  `lossless_enable` or `--lossless`. Output size depends on the content.
  `svt_jpeg_xs_encoder_get_image_config()` reports the worst-case frame
  size. Streams are declared as profile MLS.12 unless you override it.
- **Reversible colour transform on the encoder side.** The encoder now
  applies the forward RCT (Cpih=1) to planar RGB input, enabled with
  `enable_color_transform` or `--color-transform`. It requires the
  low-latency CPU profile.
- **Alpha formats.** Encoding and decoding of 4:2:2:4 (YUVA422) and
  4:4:4:4 (YUVA444, RGBA, GBRA).
- **MSB-aligned 16-bit samples.** 10/12-bit samples can sit in the high
  bits of each 16-bit word, for both input and output. Use
  `--input-msb-aligned` and `--output-msb-aligned`. This is a host-side
  memory layout only and is not signalled in the bitstream.
- **Correct profile and level in the picture header.** The encoder now
  derives ISO/IEC 21122-2 compliant Ppih and Plev values instead of
  writing zeros, which some hardware decoders rejected. You can override
  them with `--stream-profile` and `--stream-level`.
- **Compatibility with legacy hardware decoders.** New options
  `--coding-raw` and `--cap-compat`.
- **Decoder proxy mode.** Decodes at 1/2 or 1/4 resolution
  (`--proxy-mode`).
- **Log callback API.** All library output now goes through `SVT_LOG`,
  and applications can capture it with `svt_jpeg_xs_set_log_callback()`.
- **Fewer deep C-states in the decoder on Linux.** Decoder threads first
  wait in short timed slices before they block. This keeps the cores out
  of deep idle states, so throughput is higher and varies less between
  runs.
- The encoder rate control default is now `rate_control_mode = 1`, which
  gives slightly better quality.

### Platforms and build

- AArch64 support. The portable C paths and the unit tests build and run
  on AArch64, and `CPU_FLAGS_NEON` is reported there.
- macOS build support.
- MSVC 2022 detection.
- `SvtJpegxs.pc` is relocatable: its prefix is resolved from the location
  of the `.pc` file.
- New `JPEGXS_PGO` option and `RunPGO` target. `build.sh` enables LTO
  and PGO by default; use `--lto=off` or `--pgo=off` to disable them.
- Compiler and linker hardening per the Intel Secure Coding Standard:
  full RELRO, BIND_NOW and a non-executable stack for the shared
  library, `_FORTIFY_SOURCE=3`, `-fstack-clash-protection` and
  `-fcf-protection=full`; Control Flow Guard and CET compatibility for
  MSVC; stack protector and FORTIFY for MinGW.
- NASM is now the preferred assembler, and yasm is the fallback. The
  assembly objects are CET-marked (IBT, SHSTK) only when built with
  NASM 2.14.03 or newer.

### FFmpeg plugin

- Support for FFmpeg 7.0, 7.1, 8.0, 8.1 and 9.0, with patch sets and CI
  builds for Linux and Windows.
- MPEG-TS muxing and demuxing of JPEG XS.
- New encoder options:
  - `lossless`
  - `color_transform`
  - `cpu_profile`
  - `msb_aligned`
  - `coding-raw` and `cap-compat`
  - the generic `-profile` and `-level` options, mapped to Ppih and Plev
- Predefined named values for the encoder options.
- Alpha pixel formats: `yuva422p*`, `yuva444p*` and `gbrap*`.
- RGB formats are passed in R, G, B order. The decoder outputs `gbrp*`
  or `gbrap*` for RGB streams.
- If `-bpp` is not set, the encoder picks a safe default for the pixel
  format instead of failing.

### MTL plugin (renamed from imtl-plugin)

- The plugin directory is renamed to `mtl-plugin`. It requires MTL 26.09
  or newer.
- New formats:
  - YUV 4:2:0 8-bit
  - YUV 4:2:2 12-bit
  - YUV 4:4:4 10/12-bit
  - GBR 10/12-bit, with RCT support
  - YUV 4:2:2 MSB-aligned 16-bit
- A JSON configuration file, named by the `SVT_JXS_MTL_PLUGIN_CONFIG`
  environment variable, sets the encoder and decoder parameters per
  session.
- New loopback test scripts.

## Performance

- **GCLI pack and unpack (encoder and decoder).** The AVX2 and AVX-512
  paths are rewritten: empty groups are skipped through a mask, each
  group is read or written with a single load or store, and bit planes
  are spread with PEXT/PDEP.
- **AVX2+BMI2 fast path.** CPUs with AVX2 and BMI2 but without AVX-512
  now use the PDEP/PEXT GCLI path.
- **Inverse RCT inside the slice threads (decoder).** For Cpih=1
  streams, the IDWT, the inverse RCT and the output conversion now run
  per precinct in the slice threads instead of as a serial whole-frame
  stage.
- **Quantization.** New AVX2 and AVX-512 uniform quantization, an
  AVX-512 deadzone quantization, and `_mm512_lzcnt_epi32()` for the
  MSB search.
- **Vector boundary handling in the DWT and IDWT.** The wavelet
  boundary is kept in a vector register on AVX2 and AVX-512.
- **LTO and PGO.** Link-time optimisation now works on GCC 9+, Clang 12+
  and MSVC; before this release it was silently disabled on Clang. PGO
  builds are available.
- **Decoder hand-off latency.** The timed-wait change described under
  Features improves decoder throughput on Linux.
- Fixed a decoder throughput regression in the IDWT and VLC inner loops
  that the IntSan fixes had introduced.

## Fixes

### Encoder

- Reject `source_width` and `source_height` above 65535, instead of
  silently truncating them in the picture header.
- Fixed the input size check for packed formats: it now requires
  `width * 3` samples per row.
- Reject `COLOUR_FORMAT_GRAY` during configuration, instead of hitting
  an assert in Debug builds.
- Reject `slice_height = 0` in `get_image_config()` for lossless,
  matching `encoder_init()`.
- The bitstream writer now tracks positions as byte offsets, which
  removes undefined out-of-bounds pointer arithmetic.
- Fixed a shutdown hang: `svt_shutdown_process()` was missing for the
  output queue, and `get_empty_object()` now handles the quit signal.
- Fixed input image `alloc_size` checks.
- The minimum input plane size is computed in 64 bits. A large stride or
  height wrapped it, so an input buffer smaller than the rows addressed
  by the stride was accepted and read past its end.
- The CPU profile keeps the input stride in 32 bits. A stride above 65535
  was cut to 16 bits and the wrong rows were encoded.

### Decoder

- Packet mode:
  - Fixed a hang in `close()` on a partially sent frame.
  - `send_eoc()` now fails a pending partial frame instead of hanging
    `get_frame()`.
  - Fixed an endless loop when Lcod is smaller than the header.
  - `send_packet()` on a decoder initialized in frame mode now returns
    an error instead of reporting success.
  - `send_packet()` now checks the output image of the first packet of a
    frame, like `send_frame()`.
  - An empty packet (`used_size` 0) returns
    `SvtJxsErrorDecoderBitstreamTooShort` and no longer starts a frame
    with its image.
- `send_frame()` and `send_packet()` return `SvtJxsErrorBadParameter`
  for a NULL bitstream buffer instead of reading from it.
- `svt_jpeg_xs_decoder_init()` rejects headers whose coefficient count or
  component byte size does not fit in 32 bits. A 65535x65535 4:4:4
  header used to get an undersized coefficient buffer.
- Fixed a slice-overlap IDWT hang for slices with one precinct line.
- Fixed proxy mode 1/2 when `decomp_v` is 1.
- Fixed bitstream parsing.
- Fixed output image `alloc_size` checks. The minimum output plane size
  is computed in 64 bits, so a large stride or height no longer wraps it.
- Slice decode errors no longer log buffer addresses.
- Removed the false "Unexpected size of frame" warning, and log lines
  from different threads no longer interleave.
- Fixed cleanup of condition variables when decoder instance allocation
  fails.

### Common

- Added checks for the maximum number of components, NULL pointers and
  integer overflow.
- Fixed issues reported by ASan, UBSan, MSan, IntSan and TSan, including
  atomic access to `sync_num_slices_to_receive`.
- Renamed the internal `svt_log` symbol so that it no longer clashes
  with SVT-AV1 when both are linked into one binary.
- `svt_jpeg_xs_frame_pool_alloc()` now initializes the bitstream FIFO
  pointer.
- Errors about invalid input buffers passed to `send_frame()`,
  `send_packet()` and `send_picture()` are printed only at
  `verbose >= VERBOSE_ERRORS`.

### Apps

- `SvtJpegxsEncApp` and `SvtJpegxsDecApp` now check `fwrite()` and
  `fclose()` results and exit with an error instead of truncating the
  output.
- Corrected the `SvtJpegxsEncApp` help text, including the `--rc`
  default, the `--profile` values and the list of supported formats.
- `SvtJpegxsEncApp` no longer exits with 0 on Linux when the library
  fails with an error whose low byte is 0, such as
  `SvtJxsErrorInsufficientResources`; it exits with 1. Both apps now
  exit with an error when the send thread cannot be created, and
  `SvtJpegxsDecApp` when `--limit-fps` is above 240.
- `SvtJpegxsEncApp` rejects an input smaller than one frame instead of
  hanging. Input from a pipe requires `--frames`.
- `SvtJpegxsDecApp --find-bitstream-header` no longer reads past an
  input shorter than 4 bytes.

### Build

- Headers and apps are installed relative to the install prefix, so
  `cmake --install --prefix` works.
- The CTest module is only included when `BUILD_TESTING` is on.
- Fixed a multiple-definition error when building FFmpeg with
  `--enable-libsvtav1`.

### FFmpeg plugin

- The `jpegxs_pipe` parser now uses Lcod for the frame length, so a
  literal EOC+SOC sequence inside raw-coded payload no longer splits a
  frame.
- The plugin unit tests are no longer built on Windows.

### MTL plugin

- Interlaced video is encoded and decoded one field at a time.
- Fixed bugs in teardown, wake-up and error paths, and thread stop flags
  are now accessed atomically.
- The plugin builds against libraries that lack the newer encoder
  fields.
- RCT is rejected for input other than GBR.
- `build.sh` now switches the build type correctly, and the test
  scripts check their CPU and FPS inputs.
