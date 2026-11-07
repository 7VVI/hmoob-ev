cmake_minimum_required(VERSION 3.29)
project(hmmob_ev VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
  add_compile_options(/W4 /permissive- /utf-8 /EHsc)
  add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
find_package(FFmpeg REQUIRED)

add_executable(evspike src/spike/main.cpp)
target_include_directories(evspike PRIVATE src)
target_link_libraries(evspike PRIVATE
  FFmpeg::avcodec FFmpeg::avformat FFmpeg::avutil
  FFmpeg::swscale FFmpeg::swresample)

# Phase 0: run evspike with the vcpkg bin directory on PATH.
# A proper DLL deployment step lands in Phase 1.
