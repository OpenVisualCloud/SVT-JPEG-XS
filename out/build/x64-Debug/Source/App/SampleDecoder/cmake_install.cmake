# Install script for directory: C:/Users/User/source/CODECs/SVT-JPEG-XS/Source/App/SampleDecoder

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
  list(APPEND CMAKE_ABSOLUTE_DESTINATION_FILES
   "C:/Users/User/Source/CODECs/SVT-JPEG-XS/out/install/x64-Debug/bin/SvtJpegxsSampleDecoder.exe")
  if(CMAKE_WARN_ON_ABSOLUTE_INSTALL_DESTINATION)
    message(WARNING "ABSOLUTE path INSTALL DESTINATION : ${CMAKE_ABSOLUTE_DESTINATION_FILES}")
  endif()
  if(CMAKE_ERROR_ON_ABSOLUTE_INSTALL_DESTINATION)
    message(FATAL_ERROR "ABSOLUTE path INSTALL DESTINATION forbidden (by caller): ${CMAKE_ABSOLUTE_DESTINATION_FILES}")
  endif()
  file(INSTALL DESTINATION "C:/Users/User/Source/CODECs/SVT-JPEG-XS/out/install/x64-Debug/bin" TYPE EXECUTABLE FILES "C:/Users/User/source/CODECs/SVT-JPEG-XS/Bin/Debug/SvtJpegxsSampleDecoder.exe")
  if(EXISTS "$ENV{DESTDIR}/C:/Users/User/Source/CODECs/SVT-JPEG-XS/out/install/x64-Debug/bin/SvtJpegxsSampleDecoder.exe" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}/C:/Users/User/Source/CODECs/SVT-JPEG-XS/out/install/x64-Debug/bin/SvtJpegxsSampleDecoder.exe")
    if(CMAKE_INSTALL_DO_STRIP)
      execute_process(COMMAND "CMAKE_STRIP-NOTFOUND" "$ENV{DESTDIR}/C:/Users/User/Source/CODECs/SVT-JPEG-XS/out/install/x64-Debug/bin/SvtJpegxsSampleDecoder.exe")
    endif()
  endif()
endif()

