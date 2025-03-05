# Install script for directory: C:/Users/User/source/CODECs/SVT-JPEG-XS/Source/Lib

# Set the install prefix
if(NOT DEFINED CMAKE_INSTALL_PREFIX)
  set(CMAKE_INSTALL_PREFIX "C:/Users/User/Source/CODECs/SVT-JPEG-XS/out/install/x64-Debug")
endif()
string(REGEX REPLACE "/$" "" CMAKE_INSTALL_PREFIX "${CMAKE_INSTALL_PREFIX}")

# Set the install configuration name.
if(NOT DEFINED CMAKE_INSTALL_CONFIG_NAME)
  if(BUILD_TYPE)
    string(REGEX REPLACE "^[^A-Za-z0-9_]+" ""
           CMAKE_INSTALL_CONFIG_NAME "${BUILD_TYPE}")
  else()
    set(CMAKE_INSTALL_CONFIG_NAME "Debug")
  endif()
  message(STATUS "Install configuration: \"${CMAKE_INSTALL_CONFIG_NAME}\"")
endif()

# Set the component getting installed.
if(NOT CMAKE_INSTALL_COMPONENT)
  if(COMPONENT)
    message(STATUS "Install component: \"${COMPONENT}\"")
    set(CMAKE_INSTALL_COMPONENT "${COMPONENT}")
  else()
    set(CMAKE_INSTALL_COMPONENT)
  endif()
endif()

# Is this installation the result of a crosscompile?
if(NOT DEFINED CMAKE_CROSSCOMPILING)
  set(CMAKE_CROSSCOMPILING "FALSE")
endif()

# Set path to fallback-tool for dependency-resolution.
if(NOT DEFINED CMAKE_OBJDUMP)
  set(CMAKE_OBJDUMP "CMAKE_OBJDUMP-NOTFOUND")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY OPTIONAL FILES "C:/Users/User/source/CODECs/SVT-JPEG-XS/Bin/Debug/SvtJpegxs.lib")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE SHARED_LIBRARY FILES "C:/Users/User/source/CODECs/SVT-JPEG-XS/Bin/Debug/SvtJpegxs.dll")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/SvtJpegxs.dll" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/SvtJpegxs.dll")
    if(CMAKE_INSTALL_DO_STRIP)
      execute_process(COMMAND "CMAKE_STRIP-NOTFOUND" "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/SvtJpegxs.dll")
    endif()
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  include("C:/Users/User/source/CODECs/SVT-JPEG-XS/out/build/x64-Debug/Source/Lib/CMakeFiles/SvtJpegxsLib.dir/install-cxx-module-bmi-Debug.cmake" OPTIONAL)
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/pkgconfig" TYPE FILE FILES "C:/Users/User/source/CODECs/SVT-JPEG-XS/out/build/x64-Debug/SvtJpegxs.pc")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for each subdirectory.
  include("C:/Users/User/source/CODECs/SVT-JPEG-XS/out/build/x64-Debug/Source/Lib/Common/Codec/cmake_install.cmake")
  include("C:/Users/User/source/CODECs/SVT-JPEG-XS/out/build/x64-Debug/Source/Lib/Common/ASM_SSE2/cmake_install.cmake")
  include("C:/Users/User/source/CODECs/SVT-JPEG-XS/out/build/x64-Debug/Source/Lib/Common/ASM_AVX2/cmake_install.cmake")
  include("C:/Users/User/source/CODECs/SVT-JPEG-XS/out/build/x64-Debug/Source/Lib/Encoder/Codec/cmake_install.cmake")
  include("C:/Users/User/source/CODECs/SVT-JPEG-XS/out/build/x64-Debug/Source/Lib/Encoder/ASM_SSE2/cmake_install.cmake")
  include("C:/Users/User/source/CODECs/SVT-JPEG-XS/out/build/x64-Debug/Source/Lib/Encoder/ASM_SSE4_1/cmake_install.cmake")
  include("C:/Users/User/source/CODECs/SVT-JPEG-XS/out/build/x64-Debug/Source/Lib/Encoder/ASM_AVX2/cmake_install.cmake")
  include("C:/Users/User/source/CODECs/SVT-JPEG-XS/out/build/x64-Debug/Source/Lib/Encoder/ASM_AVX512/cmake_install.cmake")
  include("C:/Users/User/source/CODECs/SVT-JPEG-XS/out/build/x64-Debug/Source/Lib/Decoder/Codec/cmake_install.cmake")
  include("C:/Users/User/source/CODECs/SVT-JPEG-XS/out/build/x64-Debug/Source/Lib/Decoder/ASM_SSE4_1/cmake_install.cmake")
  include("C:/Users/User/source/CODECs/SVT-JPEG-XS/out/build/x64-Debug/Source/Lib/Decoder/ASM_AVX2/cmake_install.cmake")
  include("C:/Users/User/source/CODECs/SVT-JPEG-XS/out/build/x64-Debug/Source/Lib/Decoder/ASM_AVX512/cmake_install.cmake")

endif()

