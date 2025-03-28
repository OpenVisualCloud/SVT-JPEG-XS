# ffmpeg: enable jpegxs codec

This section explains how libSvtJpegxs can be added to FFMPEG as an external codec
for use in WSL2 / MSYS2 development environments.

Experiments with the JPEG-XS codec may require a lot of FFMPEG rebuilds, so this
guide gives an option of compiling FFMPEG with only a subset of components to
decrease compilation time to minimum.

## Linux(WSL2) ffmpeg plugin

### Notice
FFmpeg is an open source project licensed under LGPL and GPL. See 
https://www.ffmpeg.org/legal.html. You are solely responsible for determining if your 
use of FFmpeg requires any additional licenses. Intel is not responsible for obtaining 
any such licenses, nor liable for any licensing fees due, in connection with your use 
of FFmpeg.

The narrative is for a fresh installation of Ubuntu 24.04 (Noble Numbat) distribution 
into WSL2, but the section on the FFMPEG configuration can be used by 'pure Linux 
developers' who may be dealing with necessity to build FFMPEG many times a day.

### 1. Install toolchain for a fresh installation of Ubuntu 24.04:
```
sudo apt install build-essential
sudo apt install git
sudo apt install cmake
sudo apt install yasm
```
### 2. Git clone SVT-JPEG-XS
```
cd ~/
git clone https://github.com/OpenVisualCloud/SVT-JPEG-XS.git
```
### 3. Export installation location:
```
mkdir -p ~/install-dir
export INSTALL_DIR="$HOME/install-dir"
export LD_LIBRARY_PATH="$INSTALL_DIR/lib:${LD_LIBRARY_PATH}"
export PKG_CONFIG_PATH="$INSTALL_DIR/lib/pkgconfig:${PKG_CONFIG_PATH}"
```
(Do not forget to save these env vars to your profile, otherwise you have to 
make this exports with each new login.)
### 4. Build SVT-JPEG-XS
```
cd ~/SVT-JPEG-XS
cmake -B buildrelease -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=$INSTALL_DIR && \
cmake --build buildrelease -j10 --config Release --target install
```
### 5. Prepare FFmpeg build
Some components of FFmpeg require, besides yasm, also NASM compiler:
```
sudo apt install nasm
```
Git clone FFMPEG:
```
cd ~/
git clone https://git.ffmpeg.org/ffmpeg.git
```
With Ubuntu 24.04 distros, many external libraries can now be apt installed:
```
sudo apt install dav1d
sudo apt install libsdl2-dev
```
The library libsvtav1 can also be apt installed, only you have to install two
packages: 
```
sudo apt install libsvtav1-dev libsvtav1enc-dev
```
FFmpeg calls SvtAv1Enc from libsvtav1enc, but the latter requires libsvtav1
to be installed. The trick is borrowed from ref.
[SvtAv1Enc >= 0.9.0 not found using pkg-config](https://github.com/markus-perl/ffmpeg-build-script/issues/186).
If you like, you can immediately (from the console) verify that a symbol SvtAv1Enc 
is accessible:
```
sudo apt install pkgconf
pkg-config --path SvtAv1Enc
```
### 6. Checkout to branch 6.1 or 7.0 and apply patches:
  #### a) Checkout to branch/tag 6.1 and apply patches:
  ```
    git checkout release/6.1
  ```
  ```
    cp <jpeg-xs-repo>/ffmpeg-plugin/libsvtjpegxs* libavcodec/
    git am --whitespace=fix <jpeg-xs-repo>/ffmpeg-plugin/6.1/*.patch
  ```
#### OR
  #### b) Checkout to branch/tag 7.0 and apply patches:
  ```
    git checkout release/7.0
  ```
  ```
    cp <jpeg-xs-repo>/ffmpeg-plugin/libsvtjpegxs* libavcodec/
    git am --whitespace=fix <jpeg-xs-repo>/ffmpeg-plugin/7.0/*.patch
  ```

### 7. FFmpeg configure
To shrink the compilation size, we use the key --disable-everything and add only 
components necessary for routine tasks. For example, these components can be
  - enabled external libs (libsvtav1, libdav1d, libsvtjpegxs)
  - common decoders and encoders PLUS our SVT-JPEG-XS 'libsvtjpegvs' (as it is named 
	in files that we added/edited in ffmpeg distro)
  - two filters
  - common mixers/demuxers
  - SDL2 output device
  - two protocols
```
./configure --prefix=/home/User/install-dir --bindir=/home/User/bin \
--disable-doc --disable-everything --disable-network \
--enable-libsvtav1 --enable-libdav1d --enable-libsvtjpegxs \
--enable-decoder='aac,ac3,h264,hevc,libdav1d,libsvtjpegxs' \
--enable-encoder='aac,ac3,libsvtav1,wrapped_avframe,libsvtjpegxs' 
--enable-filter='scale,aresample' \
--enable-demuxer='mov,mp4,matroska,m4v,ivf,yuv4mpegpipe' \
--enable-muxer='avif,mov,matroska,m4v,yuv4mpegpipe' \
--enable-sdl2 --enable-outdev=sdl2 \
--enable-protocol='file,pipe'
```
and the 'routine tasks' are typical operations with multimedia streams, as transcode, 
open/write files, and playback streams with ffplay.

### 8. make, make install
```
make -j10
make install
```
You can use a newly build FFMPEG with libsvtjpegxs encoder.

### 8 Demonstration
Let us transcode a video file \<videofilename>.mp4 using jpegxs encoder for output. In this
example, we also change the container but you can stay with the original container, if you like:
```
$ ./ffmpeg -i \<videofilename>.mp4 -c:v jpegxs -bpp 1 \<videofilename>_jxs.mkv
```
The console output confirms that we re-encode the video stream with a `jpegxs` encoder. For example:
```
   Stream #0:0 -> #0:0 (h264 (native) -> jpegxs (libsvtjpegxs))
```
You can also verify the info with `ffprobe`.

You can replay `<videofilename>_jxs.mkv` with your newly compiled ffplay, but not with
a standard player: maybe this would change when ISO21122 becomes well establshed.

And finally, you can verify that an individual frame extracted from the video file is
an image encoded in jpegxs format: the output of this command
```
$ ./ffmpeg -ss 00:00:01 -i <videofilename>_jxs.mkv -frames:v 1 -c:v jpegxs -bpp 1 <videofilename>_oneframe.jxs
```
, `<videofilename>_oneframe.jxs`, can be decoded with SvtJpegxsDecApp or Fraunhofer's 
jxs\_decoder or viewed with a dedicated jpegxs viewer of your choice.

## Windows (MSYS2) ffmpeg plugin

### Notice
FFmpeg is an open source project licensed under LGPL and GPL. See 
https://www.ffmpeg.org/legal.html. You are solely responsible for determining 
if your use of FFmpeg requires any additional licenses. Intel is not responsible 
for obtaining any such licenses, nor liable for any licensing fees due, in connection 
with your use of FFmpeg.

### Preliminary
First, install MSYS2 and build SVT-JPEG-XS, as described in MSYS2build.md of the 
root folder.

### 1. Prepare FFmpeg build
Some components of FFmpeg require, besides yasm, also NASM compiler:
```
pacboy -S nasm:u
```
Git clone FFMPEG:
```
cd ~/
git clone https://git.ffmpeg.org/ffmpeg.git
```
External libraries can be pacboy-installed:
```
pacboy -S dav1d:u
pacboy -S svt-av1:u
pacboy -S SDL2:u
```
pkg-config is already installed (with pacboy -S toolchain:u), so you can verify 
`pkg-config --libs SvtAv1Enc`.

### 2 Checkout to branch 6.1 or 7.0 and apply patches
Exactly as in the subsection __6. What files need to be updated/added to the project__ 
of the __Linux(WSL2) ffmpeg plugin__ section this document.
### 3 FFmpeg configure

```
./configure --prefix=/home/user/install-dir --bindir=/home/user/bin \
--disable-doc --disable-everything --disable-network \
--enable-libsvtav1 --enable-libdav1d --enable-libsvtjpegxs \
--enable-decoder='aac,ac3,h264,hevc,libdav1d,libsvtjpegxs' \
--enable-encoder='aac,ac3,libsvtav1,wrapped_avframe,libsvtjpegxs' 
--enable-filter='scale,aresample' \
--enable-demuxer='mov,mp4,matroska,m4v,ivf,yuv4mpegpipe' \
--enable-muxer='avif,mov,matroska,m4v,yuv4mpegpipe' \
--enable-sdl2 --enable-outdev=sdl2 \
--enable-protocol='file,pipe'
```
and the 'routine tasks' are typical operations with multimedia streams, as transcode, 
open/write files, and playback streams with ffplay.

### 3. make, make install
The only eyebrow-raising detail of the following operations is the `make` tool name 
from the `mingw-w64-ucrt64-x86_64-toolchain` packet group -- `mingw32-make`. Is it 
somehow related to the 32-bit architecture? No more than the symbol _WIN32 in
MSVC. GNU projects use so-called "GNU triplets" to specify architecture 
information. A triplet has this format: `machine-vendor-operatingsystem`.
See explanation in the answer to SO question 
https://stackoverflow.com/questions/47585357/why-do-mingw-binaries-have-such-complicated-names-e-g-i686-w64-mingw32 .
So, mingw32 -- in this context -- is a marker of (fictitious?) operating system!
```
mingw32-make -j10
mingw32-make install
```
You can use a newly build FFMPEG with libsvtjpegxs encoder.
