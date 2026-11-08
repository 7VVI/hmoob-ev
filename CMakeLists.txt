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
  src/core/capture/DesktopSource.cpp
  src/core/capture/WindowSource.cpp
  src/core/capture/CameraSource.cpp
  src/core/gfx/VideoProcessor.cpp
  src/core/gfx/HdrToneMapper.cpp
  src/core/encoder/AmfEncoder.cpp
  src/core/mux/CodecExtradata.cpp
  src/core/mux/FfmpegMuxer.cpp
  src/core/audio/AudioMixer.cpp
  src/core/audio/AacEncoder.cpp
  src/core/audio/AudioEncoder.cpp
  src/core/audio/WasapiCapture.cpp
  src/core/preview/DesktopPreview.cpp
  src/core/recorder/Recorder.cpp
)
target_include_directories(hmmob_core PUBLIC src)
target_include_directories(hmmob_core SYSTEM PUBLIC ${FFMPEG_INCLUDE_DIRS})
target_include_directories(hmmob_core PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/third_party/amf/include")
# d3dcompiler is only used by HdrToneMapper.cpp, so it stays PRIVATE; CMake
# still propagates it as LINK_ONLY to consumers of this static library.
# mmdevapi/ole32/avrt are WASAPI capture dependencies (avrt for MMCSS).
# windowsapp is the Windows Runtime umbrella lib (RoInitialize, WinRT
# activation used by WindowSource). mfplat/mfreadwrite/mf/mfuuid are the Media
# Foundation libraries used by CameraSource.
target_link_libraries(hmmob_core
  PUBLIC d3d11 dxgi mmdevapi ole32 avrt ${FFMPEG_LIBRARIES}
  PRIVATE d3dcompiler windowsapp mfplat mfreadwrite mf mfuuid)

add_executable(evspike src/spike/main.cpp)
target_include_directories(evspike SYSTEM PRIVATE ${FFMPEG_INCLUDE_DIRS})
# dwmapi is used by the --window enumerator to skip cloaked windows.
target_link_libraries(evspike PRIVATE hmmob_core ${FFMPEG_LIBRARIES} dwmapi)

# Qt recording window.
find_package(Qt6 REQUIRED COMPONENTS Widgets)
qt_standard_project_setup()

qt_add_executable(hmmob_ev_app WIN32
  src/app/main.cpp
  src/app/MainWindow.cpp src/app/MainWindow.h
  src/app/PreviewWidget.cpp src/app/PreviewWidget.h
  src/app/RegionSelector.cpp src/app/RegionSelector.h
  src/app/WindowPickerDialog.cpp src/app/WindowPickerDialog.h
  src/app/IconRenderer.cpp src/app/IconRenderer.h
  src/app/LevelMeter.cpp src/app/LevelMeter.h
  src/app/NavTabButton.cpp src/app/NavTabButton.h
  src/app/ModeTile.cpp src/app/ModeTile.h
  src/app/TitleBar.cpp src/app/TitleBar.h
  src/app/SettingsDialog.cpp src/app/SettingsDialog.h
  src/app/AppSettings.cpp src/app/AppSettings.h
  src/app/Theme.cpp src/app/Theme.h
  src/app/Controls.cpp src/app/Controls.h
  src/app/UiFont.h)
target_link_libraries(hmmob_ev_app PRIVATE hmmob_core Qt6::Widgets dwmapi shell32)

# vcpkg's applocal deployment only copies DLLs the exe imports directly; Qt
# discovers plugins at run time, so the platform/style plugins must be copied
# next to the executable explicitly. Derive the Qt prefix from the imported
# Qt6::Core location, i.e. <prefix>/bin/Qt6Core.dll.
get_target_property(_qt_core_location Qt6::Core IMPORTED_LOCATION_RELEASE)
if(NOT _qt_core_location)
  get_target_property(_qt_core_location Qt6::Core IMPORTED_LOCATION)
endif()
if(_qt_core_location)
  get_filename_component(_qt_bin_dir "${_qt_core_location}" DIRECTORY)
  get_filename_component(_qt_prefix "${_qt_bin_dir}" DIRECTORY)
else()
  # <prefix>/share/Qt6 -> <prefix>
  get_filename_component(_qt_prefix "${Qt6_DIR}/../.." ABSOLUTE)
endif()

set(_qt_plugins_dir "${_qt_prefix}/${QT6_INSTALL_PLUGINS}")
if(NOT EXISTS "${_qt_plugins_dir}")
  set(_qt_plugins_dir "${_qt_prefix}/plugins")
endif()

if(NOT EXISTS "${_qt_plugins_dir}/platforms")
  message(FATAL_ERROR
    "Qt platform plugins not found at '${_qt_plugins_dir}/platforms'. "
    "The Qt runtime cannot start without them; check QT6_INSTALL_PLUGINS "
    "and the vcpkg Qt installation.")
endif()

set(_qt_deploy_commands
  COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${_qt_plugins_dir}/platforms"
    "$<TARGET_FILE_DIR:hmmob_ev_app>/platforms")
if(EXISTS "${_qt_plugins_dir}/styles")
  list(APPEND _qt_deploy_commands
    COMMAND ${CMAKE_COMMAND} -E copy_directory
      "${_qt_plugins_dir}/styles"
      "$<TARGET_FILE_DIR:hmmob_ev_app>/styles")
endif()
add_custom_command(TARGET hmmob_ev_app POST_BUILD
  ${_qt_deploy_commands}
  COMMENT "Deploying Qt platform and style plugins")

include(CTest)
if(BUILD_TESTING)
  find_package(Catch2 3 REQUIRED)
  add_executable(hmmob_tests
    tests/test_qpc_clock.cpp
    tests/test_frame_queue.cpp
    tests/test_d3d11_device.cpp
    tests/test_dxgi_capture.cpp
    tests/test_video_processor.cpp
    tests/test_hdr_tone_mapper.cpp
    tests/test_amf_encoder.cpp
    tests/test_codec_extradata.cpp
    tests/test_ffmpeg_muxer.cpp
    tests/test_pcm_ring_buffer.cpp
    tests/test_audio_mixer.cpp
    tests/test_aac_encoder.cpp
    tests/test_pcm_encoder.cpp
    tests/test_wasapi_capture.cpp
    tests/test_desktop_preview.cpp
    tests/test_window_source.cpp
    tests/test_camera_source.cpp
    tests/test_recorder.cpp
  )
  target_link_libraries(hmmob_tests PRIVATE hmmob_core Catch2::Catch2WithMain)
  include(Catch)
  # The WindowSource integration test can block inside Windows Graphics
  # Capture on some machines; a per-test timeout keeps ctest from hanging.
  catch_discover_tests(hmmob_tests ADD_TAGS_AS_LABELS
    PROPERTIES TIMEOUT 120)
endif()

# vcpkg applocal deploys FFmpeg DLLs next to evspike.exe. Install-time deployment lands in Phase 1.
