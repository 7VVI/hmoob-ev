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

if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/amf/include/core/Factory.h")
  message(FATAL_ERROR
    "AMF headers not found in third_party/amf/include. "
    "Run scripts/fetch_amf.ps1 to download them.")
endif()

add_library(hmmob_core STATIC
  src/core/common/QpcClock.cpp
  src/core/gfx/D3D11Device.cpp
  src/core/capture/DxgiCapture.cpp
  src/core/gfx/VideoProcessor.cpp
  src/core/encoder/AmfEncoder.cpp
  src/core/mux/CodecExtradata.cpp
  src/core/mux/FfmpegMuxer.cpp
)
target_include_directories(hmmob_core PUBLIC src)
target_include_directories(hmmob_core SYSTEM PUBLIC ${FFMPEG_INCLUDE_DIRS})
target_include_directories(hmmob_core PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/third_party/amf/include")
target_link_libraries(hmmob_core PUBLIC d3d11 dxgi ${FFMPEG_LIBRARIES})

add_executable(evspike src/spike/main.cpp)
target_include_directories(evspike SYSTEM PRIVATE ${FFMPEG_INCLUDE_DIRS})
target_link_libraries(evspike PRIVATE hmmob_core ${FFMPEG_LIBRARIES})

include(CTest)
if(BUILD_TESTING)
  find_package(Catch2 3 REQUIRED)
  add_executable(hmmob_tests
    tests/test_qpc_clock.cpp
    tests/test_frame_queue.cpp
    tests/test_d3d11_device.cpp
    tests/test_dxgi_capture.cpp
    tests/test_video_processor.cpp
    tests/test_amf_encoder.cpp
    tests/test_codec_extradata.cpp
    tests/test_ffmpeg_muxer.cpp
  )
  target_link_libraries(hmmob_tests PRIVATE hmmob_core Catch2::Catch2WithMain)
  include(Catch)
  catch_discover_tests(hmmob_tests ADD_TAGS_AS_LABELS)
endif()

# vcpkg applocal deploys FFmpeg DLLs next to evspike.exe. Install-time deployment lands in Phase 1.
