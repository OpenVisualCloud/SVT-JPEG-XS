# Building SVT-JPEG-XS with MSYS2 

We recommend to begin with a fresh installation of MSYS2.

1. Download an installer from the front page of [MSYS2](https://www.msys2.org), run the 
   installer.
2. When done, click Finish __without__ the `Run MSYS2 now` control selected (there can be
   issues when the application is started immediately after installation), or do `$ exit`
   from the first run of the terminal, if `Run MSYS2 now` was selected when you finished.
3. Run MSYS2 UCRT64 terminal. All MSYS2 commands of this guide must be run in the 
   __MSYS2 UCRT64__ terminal only!
4. Update the installation with the command `pacman -Suy`
5. Many packages installed in this guide have long names, like 
   `mingw-w64-ucrt-x86_64-ninja`. You can avoid writing long package names while using `pacboy`
   command, see [Package Naming](https://www.msys2.org/docs/package-naming/):
   - Install the pactoys packet group, in which pacboy belongs: `pacman -S pactoys`
6. Install the packet group __mingw-w64-ucrt-x86_64-toolchain__, the packets __yasm__ and __cmake__:
   - Install toolchain: `pacboy -S toolchain:u`
     If you are not sure about which packets you have to choose from this group, use the 
     default option `select all`. 
   - Install yasm (`pacboy -S yasm:u`) and cmake (`pacboy -S cmake:u`).
     See all the packages installed: `pacman -Q`.
7. Using pacman, install git (`pacman -S git`). __Notice that this operation uses 
   pacman, not pacboy command__!
8. Git clone the SVT-JPEG-XS repository to your computer and generate debug/release 
   configuration of your choise:
    ```
    git clone https://github.com/OpenVisualCloud/SVT-JPEG-XS.git
    ```
   - Change the current working directory: 
    ```
    cd ~/SVT-JPEG-XS
    ```
   - Run cmake command, specify a source directory (`builddebug`) to generate a build system for the Debug configuration
    ```
    cmake -S . -B builddebug -DCMAKE_BUILD_TYPE=Debug -DCMAKE_SHARED_LIBS=OFF -DCMAKE_INSTALL_PREFIX=$HOME/install-dir
    ```
   - OR specify a source directory (`buildrelease`) for the Release configuration
    ```
    cmake -S . -B buildrelease -DCMAKE_BUILD_TYPE=Release -DCMAKE_SHARED_LIBS=OFF -DCMAKE_INSTALL_PREFIX=$HOME/install-dir
    ```
   - with the above two cmake commands, you can include any relevant cmake keys 
     you may find necessary for your project.
   - Build Debug or Release configuration
   - `cmake -j4 --build builddebug --config Debug --target install`
   - OR
   - `cmake -j4 --build buildrelease --config Release --target install`

- To test the build, you can run a newly installed program, for example
  ```
  $ /home/User/install-dir/bin/SvtJpegxsSampleDecoder <path/filename.jxs>
  ```

## Encoder

### Supported colour formats

#### PLANAR

| format | EncApp: format + input-depth| ffmpeg name |status |
| -- | -- | -- | -- |
| YUV 420 8-bit | yuv420 + 8 | yuv420p | Tested, working properly
| YUV 420 10/12/14-bit |yuv420 + 10/12/14 | yuv420p{10/12/14}le | Tested, working properly
| YUV 422 8-bit | yuv422 + 8 |yuv422p | Tested, working properly
| YUV 422 10/12/14-bit |yuv422 + 10/12/14 | yuv422p{10/12/14}le | Tested, working properly
| YUV 444 8-bit | yuv444 + 8 | yuv444p | Tested, working properly
| YUV 444 10/12/14-bit | yuv444 + 10/12/14 |yuv444p{10/12/14}le | Tested, working properly
| RGB 8-bit | rgb  + 8 | gbrp | Tested, working properly
| RGB 10/12/14-bit |rgb + 10/12/14 | gbrp{10/12/14}le | Tested, working properly
| YUV 400 8-bit | yuv400 + 8| - | Unsupported
| YUV 400 10-bit |yuv400 + 10/12/14 | - | Unsupported


#### PACKED
| format | EncApp: format + input-depth| ffmpeg name |status |
| -- | -- | -- | -- |
| RGB 8bit| rgbp + 8 | rgb24/bgr24 | Tested, working properly, **decoder decodes to planar format only**
| RGB 10/12/14bit |rgbp + 10/12/14|   - | Tested, working properly, **decoder decodes to planar format only**


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

To encode stream with compression rate of 12:1 (assuming 8bit yuv420p), one pixel require 12bits so `--bpp 1 ` should be used:

```shell
./SvtJpegxsEncApp -i <input_file.yuv> -b <output_bitstream.bin> -w 1920 -h 1080 --input-depth 8 --colour-format yuv420 --bpp 1 --decomp_v 2 --decomp_h 5 --lp 4
```

#### Latency measurement

To measure average frame encoding time (latency) `--limit-fps`  and high enough `--lp` parameters have to be used, cmd example:

```shell
./SvtJpegxsEncApp -i <input_file.yuv> -b <output_bitstream.bin> -w 1920 -h 1080 --input-depth 8 --colour-format yuv422 --bpp 5 --decomp_v 2 --decomp_h 5 --lp 4 --limit-fps 60
```

The `--limit-fps` must be lower than the maximum achievable number of frames. So that the next frames are not queued (time of waiting frames for processing is included in the latency time).

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
--colour-format            Set encoder colour format (yuv420, yuv422,  yuv444, rgb(planar), rgbp(packed))
                            (Experimental: yuv400)
--input-depth              Input depth
--bpp                      Bits Per Pixel, can be passed as integer or float
                            (example: 0.5, 3, 3.75, 5 etc.)
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
[--rc]                     Rate Control mode (
                            CBR: budget per precinct: 0,
                            CBR: budget per precinct with padding movement: 1,
                            CBR: budget per slice: 2,
                            CBR: budget per slice with nax size RATE: 3,
                            default 0)
```

Threading, performance:

```text
[--asm]                    Limit assembly instruction set [0 - 11] or [c, mmx, sse, sse2,
                            sse3, ssse3, sse4_1, sse4_2, avx, avx2, avx512, max], by default
                            highest level supported by CPU
[--profile]                Threading model type, can be passed as digit (0, 1) or string
                            (latency, cpu) 0:latency (Low Latency mode), 1:cpu (Low CPU use
                            mode), default is 0
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

The `--limit-fps` must be lower than the maximum achievable number of frames. So that the next frames are not queued (time of waiting frames for processing is included in the latency time).

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
```

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

## Encoder and Decoder design

Please see [Encoder design](documentation/encoder/svt-jpegxs-encoder-design.md)

Please see [Decoder design](documentation/decoder/svt-jpegxs-decoder-design.md)


## API usage

Please see [Encoder snippet](documentation/encoder/EncoderSnippets.md) for encoder structure overview and simplified encoder usage.

Please see [Decoder snippet](documentation/decoder/DecoderSnippets.md) for decoder structure overview and simplified decoder usage.

## Notes

The information in this document was compiled at <mark>v0.9</mark> may not
reflect the latest status of the design. For the most up-to-date
settings and implementation, it's recommended to visit the section of the code.
