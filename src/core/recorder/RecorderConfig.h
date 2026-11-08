#pragma once
#include <cstdint>
#include <string>

#include "core/audio/AudioCodec.h"

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
    // WASAPI endpoint ids from WasapiCapture::EnumerateDevices. Empty selects
    // the default endpoint of that flow; an id that no longer exists disables
    // just that audio source and reports a warning.
    std::string system_device_id;
    std::string mic_device_id;
    // Audio track format. The container is chosen from output_path's
    // extension; sample_rate must be 44100 or 48000 and bitrate is used for
    // AAC only (96000..320000).
    AudioCodec audio_codec = AudioCodec::Aac;
    int audio_sample_rate = 48000;
    int audio_bitrate = 192000;
    std::string output_path;
    // 0 = record until RequestStop(); > 0 = stop after this many seconds.
    int duration_seconds = 0;
    int preview_width = 640;
    int preview_height = 360;
    int preview_fps = 30;
};

}  // namespace hmmob
