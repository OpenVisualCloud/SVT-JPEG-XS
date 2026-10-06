# svt-jpeg-xs plugin library

## 1. Build

### 1.1 Build and install Intel Media Transport Library

Please refer to https://github.com/OpenVisualCloud/Media-Transport-Library/blob/main/doc/build.md

### 1.2 Build and install svt-jpeg-xs

```bash
cd <jpeg-xs-repo>/Build/linux
./build.sh install
```

### 1.3 Build and install imtl-plugin

Requires json-c (already a dependency of Media Transport Library).

```bash
cd <jpeg-xs-repo>/imtl-plugin
./build.sh
```

## 2. Test

### 2.1 Prepare a yuv422p8le file

```bash
wget https://www.larmoire.info/jellyfish/media/jellyfish-3-mbps-hd-hevc.mkv
ffmpeg -i jellyfish-3-mbps-hd-hevc.mkv -vframes 3 -c:v rawvideo yuv420p8le.yuv
ffmpeg -s 1920x1080 -pix_fmt yuv420p -i yuv420p8le.yuv -pix_fmt yuv422p test_planar8.yuv
```

### 2.2 Edit "IMTL-repo/kahawai.json" to enable the st22 svt jpeg xs plugin

You can also copy kahawai.json from ```<jpeg-xs-repo>/imtl-plugin/kahawai.json```

```json
        {
            "enabled": 1,
            "name": "st22_svt_jpeg_xs",
            "path": "/usr/local/lib/x86_64-linux-gnu/libst_plugin_st22_svt_jpeg_xs.so"
        },
        {
            "enabled": 1,
            "name": "st22_svt_jpeg_xs",
            "path": "/usr/local/lib64/libst_plugin_st22_svt_jpeg_xs.so"
        }
```

### 2.3 Run the imtl sample with tx and rx based on jpegxs

Tx run:

```bash
<IMTL-repo>/build/app/TxSt22PipelineSample --st22_codec jpegxs --pipeline_fmt YUV422PLANAR8 --p_port 0000:31:00.0 --tx_url test_planar8.yuv
```

Rx run:

```bash
<IMTL-repo>/build/app/RxSt22PipelineSample --st22_codec jpegxs --pipeline_fmt YUV422PLANAR8 --p_port 0000:31:00.1 --rx_url out_planar8.yuv
```

## 3. Configuration

The plugin takes optional encoder/decoder settings from a JSON file, loaded once at plugin start
from the path in the `SVT_JXS_MTL_PLUGIN_CONFIG` environment variable. Without the variable the
plugin runs with built-in defaults.

```bash
export SVT_JXS_MTL_PLUGIN_CONFIG=<jpeg-xs-repo>/imtl-plugin/sample_config.json
```

`sample_config.json` lists every key with its allowed values and default; comments are allowed in
the file. Key names follow `SvtJpegxsEncApp` / `SvtJpegxsDecApp` options (`--lp`, `--asm`,
`--decomp_v`, `--rc`, `--lossless`, `--stream-profile`, ...), so settings tuned with the apps can be
copied.

* All keys are optional. A missing key keeps the built-in default, or the value set by the MTL
  quality mode: SPEED sets `decomp_v` 0, QUALITY sets `quantization` uniform and `slice_height` 64.
  A key set in the file overrides the quality mode.
* Settings apply to all encoder/decoder sessions of the process.
* `log_level` (`error`, `warning`, `info`, `debug`) filters plugin messages and sets the SVT-JPEG-XS
  library verbosity: `error` shows library errors only, `warning` and `info` keep the library
  default, `debug` adds library warnings and multithreading info. With `debug` the decoder logs a
  warning per slice, which costs performance.
* An unknown key, an invalid value, a syntax error, or a key the installed SVT-JPEG-XS library does
  not support makes plugin start fail; the error names the key and the allowed values. Invalid
  combinations (e.g. `rct` with 4:2:2 input) make only the affected session fail.
* With `lossless` the frame size depends on the content; the plugin reports the worst case size to
  MTL. MTL allocates buffers and reserves Tx bandwidth for that size, close to uncompressed, so the
  session may not fit the link. The receiver must set `max_codestream_size` to the worst case size
  too, larger frames are dropped otherwise.

Resolution, pixel format, bit depth, bpp (from the MTL codestream size) and packetization are set by
MTL and can't be configured.

## 4. Notes

If you get below similar message when runing the RxTxApp, it's likely a ld library path problem.

```text
./build/app/RxTxApp: error while loading shared libraries: librte_dmadev.so.23: cannot open shared object file: No such file or directory
```

In this case you might need to update your LD_LIBRARY_PATH

```bash
export LD_LIBRARY_PATH=/usr/local/lib:/usr/local/lib/x86_64-linux-gnu:$LD_LIBRARY_PATH
```

If you get below similar message when runing the RxTxApp, you need to update hugepage size

```text
EAL: FATAL: Cannot get hugepage information.
EAL: Cannot get hugepage information.
```

Please run

```bash
sudo sysctl -w vm.nr_hugepages=2048
```
