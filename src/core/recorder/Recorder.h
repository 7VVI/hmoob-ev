#pragma once
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "core/preview/PreviewFrame.h"

namespace hmmob {

struct RecorderConfig {
    int display_index = 0;
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

struct RecorderStats {
    int64_t elapsed_us = 0;
    int acquired = 0;
    int new_desktop_frames = 0;
    int encoded_frames = 0;
    int packets_written = 0;
    int64_t encoded_bytes = 0;
    int encode_errors = 0;
    int audio_packets = 0;
    int64_t audio_bytes = 0;
    int target_frames = 0;
    bool audio_track = false;
    // Resolved capture format; valid once Start() has returned true.
    int display_width = 0;
    int display_height = 0;
    int display_format = 0;
};

class Recorder {
public:
    Recorder();
    ~Recorder();
    Recorder(const Recorder&) = delete;
    Recorder& operator=(const Recorder&) = delete;

    [[nodiscard]] bool Start(const RecorderConfig& config, std::string& error);
    void RequestStop();
    void Stop();
    bool Running() const;
    bool Finished() const;
    std::string error() const;
    RecorderStats stats() const;
    std::shared_ptr<const PreviewFrame> LatestPreview() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace hmmob
