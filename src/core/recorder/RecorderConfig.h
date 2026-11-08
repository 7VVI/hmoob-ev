#pragma once
#include <cstdint>
#include <string>

namespace hmmob {

enum class CaptureMode { Screen, Region, Window, Camera };

struct RecorderConfig {
    CaptureMode mode = CaptureMode::Screen;
    int display_index = 0;
    // Region mode: the rectangle inside the captured display, in pixels.
    int region_x = 0;
    int region_y = 0;
    int region_width = 0;
    int region_height = 0;
    void* window = nullptr;    // HWND for Window mode (later task)
    int camera_index = 0;      // device index for Camera mode (later task)
    int fps = 60;
    std::string codec = "hevc";  // "hevc" or "h264"
    int64_t bitrate = 50000000;
    bool hdr = false;
    bool system_audio = true;
    bool microphone = true;
    float system_gain = 1.0f;
    float mic_gain = 1.0f;
    std::string output_path;
    // 0 = record until RequestStop(); > 0 = stop after this many seconds.
    int duration_seconds = 0;
    int preview_width = 640;
    int preview_height = 360;
    int preview_fps = 30;
};

}  // namespace hmmob
