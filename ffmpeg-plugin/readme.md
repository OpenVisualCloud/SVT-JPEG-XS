# Linux ffmpeg plugin

## Notice
FFmpeg is an open source project licensed under LGPL and GPL. See https://www.ffmpeg.org/legal.html. You are solely responsible for determining if your use of FFmpeg requires any additional licenses. Intel is not responsible for obtaining any such licenses, nor liable for any licensing fees due, in connection with your use of FFmpeg.

## 0. Create installation directory and export env variable:
```
mkdir install-dir
export INSTALL_DIR=$PWD/install-dir
```
## 1. Compile and install svt-jpegxs:
```
cd <jpeg-xs-repo>/Build/linux
./build.sh install --prefix $INSTALL_DIR
```
## 2. Export installation location:
```
export LD_LIBRARY_PATH="$INSTALL_DIR/lib:${LD_LIBRARY_PATH}"
export PKG_CONFIG_PATH="$INSTALL_DIR/lib/pkgconfig:${PKG_CONFIG_PATH}"
```
## 3. Download/Compile Ffmpeg:
### a) Clone repository:
```
git clone https://git.ffmpeg.org/ffmpeg.git ffmpeg
cd ffmpeg
```
### b) Checkout to branch/tag 6.1:
```
 git checkout release/6.1
```
### c) apply jpeg-xs plugin patches:
  ```
  cp <jpeg-xs-repo>/ffmpeg-plugin/libsvtjpegxs* libavcodec/
  git am --whitespace=fix <jpeg-xs-repo>/ffmpeg-plugin/6.1/*.patch
  ```
### d) Configure:
```
./configure --enable-libsvtjpegxs --prefix=$INSTALL_DIR --enable-shared
```
### e) build and install:
```
make -j40
make install
```

Note, for ffmpeg 7.0 version, replace 6.1 with 7.0 for above example commands.

## 4. Test executable:
Binary (executable) is located in main ffmpeg directory or ```$INSTALL_DIR/bin/```


# Windows ffmpeg plugin

## 1. Download and install binary/installer from: https://www.msys2.org/
## 2. Open terminal MINGW64

<installation_path>\msys64\mingw64.exe
*If an error with lack of support for MSYS environment is encountered, please ensure MINGW64 is used. You can switch to it from any shell by
```source shell mingw64```. Your selected shell is written after a machine name e.g. ```user@user-mobl MSYS ~```

## 3.  Configure new environment for MINGW64:
### a) Export proxy(optional, if required):
```
export ftp_proxy=<ftp>
export http_proxy=<http>
export https_proxy=<https>
```
### b) Install packages:
```
pacman -S make mingw-w64-x86_64-gcc mingw-w64-x86_64-cmake mingw-w64-x86_64-yasm mingw-w64-x86_64-diffutils
```
## 4. Create installation directory and export env variable:
```
mkdir install-dir
export INSTALL_DIR=$PWD/install-dir
```
## 5. Compile and install svt-jpeg-xs libs(In main svt-jpegxs folder)
### a) Configure:
```
cmake -S . -B svtjpegxs-build -DBUILD_APPS=off -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF -DCMAKE_INSTALL_PREFIX=$INSTALL_DIR
```
### b) Build:
```
cmake --build svtjpegxs-build -j10 --config Release --target install
```
## 6. Download/Compile Ffmpeg:
### a) Clone repository
```
git clone https://git.ffmpeg.org/ffmpeg.git ffmpeg
cd ffmpeg
```
### b) checkout to branch/tag 6.1:
 ```
 git checkout release/6.1
```
### c) apply plugin patches:
  ```
  cp <jpeg-xs-repo>/ffmpeg-plugin/libsvtjpegxs* libavcodec/
  git am --whitespace=fix <jpeg-xs-repo>/ffmpeg-plugin/6.1/*.patch
```
### d) Export path for svt-jpeg-xs installation directory:
```
export PKG_CONFIG_PATH="$INSTALL_DIR/lib/pkgconfig:${PKG_CONFIG_PATH}"
```
### e) Configure Release Build:
```
./configure --enable-libsvtjpegxs --prefix=$INSTALL_DIR --enable-static --disable-shared
```
*If a ```SvtJpegxs >= X.X.X not found using pkg-config``` error happens during FFmpeg's configuration step - ensure ```$INSTALL_DIR``` contains the proper path, where a following folder-file structure is present:
```
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
### f) build:
```
make -j10
```

Note, for ffmpeg 7.0 version, replace 6.1 with 7.0 for above example commands.

# How to use ffmpeg with jpeg-xs

## libsvtjpegxs encoder available params:

Name | mandatory/optional | Accepted values | description
  --         |     --    |                           --                                     |  --
bpp          | mandatory | any integer/float greater than 0 (example: 0.5, 3, 3.75, 5 etc.) | Bits Per Pixel
decomp_v     | optional  | 0, 1, 2(default)                                                 | Number of Vertical decompositions
decomp_h     | optional  | 0, 1, 2, 3, 4, 5(default)                                        | Number of Horizontal decompositions, have to be greater or equal to decomp_v
threads      | optional  | Any integer in range< 1;64>                                      | Number of threads encoder can create
slice_height | optional  | (default:16), Any integer in range <1;source_height>, also it have to be multiple of 2^(decomp_v)) | Coding feature: Specify slice height in units of picture luma pixels
quantization | optional  | (default:deadzone), 0(deadzone), 1(uniform)                    | Coding feature: Quantization method
coding-signs | optional  | (default:off), 0(off), 1(fast), 2(full)                        | Coding feature: Sign handling strategy
coding-sigf  | optional  | (default:on), 0(off), 1(on)                                    | Coding feature: Significance coding
coding-vpred | optional  | (default:off), 0(off), 1(on)                                   | Coding feature: Vertical-prediction

## libsvtjpegxs decoder available params:
Name | mandatory/optional | Accepted values | description
  --     |     --    |               --                                                | --
threads  | optional  | Any integer in range< 1;64>                                     | Number of threads decoder can create

### Encoding raw video:
```
./ffmpeg.exe -y -s:v 1920x1080 -c:v rawvideo -pix_fmt yuv420p -i <raw_stream.yuv> -codec jpegxs -bpp 1.25 <more encoder params -threads 5> encoded_file.mov
```

### Playback encoded stream via ffplay
```
./ffplay.exe encoded_file.mov -threads 4
```

Bitstream can be stored in *.mkv, *.mp4, *.mov containers

### Decoding jpegxs streams to raw video
```
./ffmpeg.exe -threads 10 -i <jpegxs-file.mov> <output.yuv>
```

### Transcoding from any format to jpegxs
```
./ffmpeg.exe -i <input.mov/.mp4/.mkv> -c:v jpegxs -bpp 2 -threads 15 encoder.mov
```
