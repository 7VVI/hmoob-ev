cmake_minimum_required(VERSION 3.29)
project(hmmob_ev VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
  add_compile_options(/W4 /permissive- /utf-8 /EHsc)
  add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
endif()

find_package(FFMPEG REQUIRED)

add_executable(evspike src/spike/main.cpp)
target_include_directories(evspike PRIVATE src ${FFMPEG_INCLUDE_DIRS})
target_link_libraries(evspike PRIVATE ${FFMPEG_LIBRARIES})

# vcpkg applocal deploys FFmpeg DLLs next to evspike.exe. Install-time deployment lands in Phase 1.
