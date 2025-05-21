@echo off
REM filepath: /home/labrat/lgrab/SVT-JPEG-XS/ffmpeg-plugin/build_ffmpeg_svtjpegxs.bat

REM Usage: build_ffmpeg_svtjpegxs.bat <jpeg-xs-repo-path> <ffmpeg-version: 6.1|7.0>
setlocal


set "JPEGXS_REPO=%CD%"
set "FFMPEG_VERSION=%~1"
if "%FFMPEG_VERSION%"=="" set "FFMPEG_VERSION=6.1"

if /I not "%FFMPEG_VERSION%"=="6.1" if /I not "%FFMPEG_VERSION%"=="7.0" (
    echo Usage: build_ffmpeg_svtjpegxs.bat ^<jpeg-xs-repo-path^> ^<ffmpeg-version: 6.1^|7.0^>
    exit /b 1
)

set "INSTALL_DIR=%CD%\install-dir"
if not exist "%INSTALL_DIR%" mkdir "%INSTALL_DIR%"

REM 1. Compile and install svt-jpegxs
cd /d "%JPEGXS_REPO%"
cmake -S . -B svtjpegxs-build -DBUILD_APPS=off -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF -DCMAKE_INSTALL_PREFIX=%INSTALL_DIR%
cmake --build svtjpegxs-build -j10 --config Release --target install

REM 2. Set PKG_CONFIG_PATH
set "PKG_CONFIG_PATH=%INSTALL_DIR%\lib\pkgconfig;%PKG_CONFIG_PATH%"

REM 3. Download/Compile FFmpeg
cd /d "%CD%"
git config --global user.email "runner@github.com"
git config --global user.name "action-runner"

git clone https://git.ffmpeg.org/ffmpeg.git ffmpeg-%FFMPEG_VERSION%
cd ffmpeg-%FFMPEG_VERSION%
git checkout release/%FFMPEG_VERSION%

REM 4. Apply jpeg-xs plugin patches
copy "%JPEGXS_REPO%\ffmpeg-plugin\libsvtjpegxs*" libavcodec\
git am --whitespace=fix "%JPEGXS_REPO%\ffmpeg-plugin\%FFMPEG_VERSION%\*.patch"

REM 5. Configure FFmpeg
bash -c "./configure --enable-libsvtjpegxs --prefix=%INSTALL_DIR% --enable-static --disable-shared"

REM 6. Build FFmpeg
make -j10

echo Build complete! FFmpeg binary is in %INSTALL_DIR%\bin\
endlocal