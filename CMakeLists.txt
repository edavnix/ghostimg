cmake_minimum_required(VERSION 4.2)
project(ghostimg VERSION 1.0.0 LANGUAGES C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

find_package(JPEG REQUIRED)   # FindJPEG.cmake — libjpeg / libjpeg-turbo
find_package(PNG  REQUIRED)   # FindPNG.cmake  — libpng + zlib

# WebP: no standard CMake module, locate headers and libs manually
find_path(WEBP_INCLUDE_DIR
  NAMES webp/encode.h webp/decode.h
  REQUIRED
)

find_library(WEBP_LIBRARY      NAMES webp      REQUIRED)
find_library(WEBPDEMUX_LIBRARY NAMES webpdemux)
find_library(WEBPMUX_LIBRARY   NAMES webpmux)
find_library(SHARPYUV_LIBRARY  NAMES sharpyuv)

add_executable(ghostimg
  src/io.c
  src/args.c
  src/platform.c
  src/batch.c
  src/jpeg.c
  src/png.c
  src/webp.c
  src/main.c
)

# Headers
target_include_directories(ghostimg PRIVATE
  include
  ${JPEG_INCLUDE_DIRS}
  ${PNG_INCLUDE_DIRS}
  ${WEBP_INCLUDE_DIR}
)

# Linking
target_link_libraries(ghostimg PRIVATE
  ${JPEG_LIBRARIES}
  ${PNG_LIBRARIES}
  ${WEBP_LIBRARY}
)

if(WEBPDEMUX_LIBRARY)
  target_link_libraries(ghostimg PRIVATE ${WEBPDEMUX_LIBRARY})
endif()

if(WEBPMUX_LIBRARY)
  target_link_libraries(ghostimg PRIVATE ${WEBPMUX_LIBRARY})
endif()

if(SHARPYUV_LIBRARY)
  target_link_libraries(ghostimg PRIVATE ${SHARPYUV_LIBRARY})
endif()

# Compiler warnings
if(MSVC)
  target_compile_options(ghostimg PRIVATE /W4)
else()
  target_compile_options(ghostimg PRIVATE
    -Wall -Wextra -Wpedantic -Wshadow
    -Wformat=2 -Wconversion -Wsign-conversion
    -Wstrict-prototypes -Wmissing-prototypes
  )
  # -static-libgcc is GNU ld only — not supported by Apple clang
  if(NOT APPLE AND NOT MSVC)
    target_link_options(ghostimg PRIVATE -static-libgcc)
  endif()
endif()

# Sanitizers on Debug (Linux/macOS only)
if(CMAKE_BUILD_TYPE STREQUAL "Debug" AND NOT MSVC AND NOT WIN32)
  target_compile_options(ghostimg PRIVATE -fsanitize=address,undefined)
  target_link_options(ghostimg    PRIVATE -fsanitize=address,undefined)
endif()

# Release: optimize + strip
if(CMAKE_BUILD_TYPE STREQUAL "Release" AND NOT MSVC)
  target_compile_options(ghostimg PRIVATE -O2)
  if(NOT APPLE)
    target_link_options(ghostimg PRIVATE -s)
  endif()
endif()

install(TARGETS ghostimg RUNTIME DESTINATION bin)

message(STATUS "ghostimg ${PROJECT_VERSION} | ${CMAKE_SYSTEM_NAME} | ${CMAKE_BUILD_TYPE}")
message(STATUS "  JPEG include : ${JPEG_INCLUDE_DIRS}")
message(STATUS "  PNG  include : ${PNG_INCLUDE_DIRS}")
message(STATUS "  WebP include : ${WEBP_INCLUDE_DIR}")
message(STATUS "  WebP lib     : ${WEBP_LIBRARY}")
