#pragma once
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "core/preview/PreviewFrame.h"
#include "core/recorder/RecorderConfig.h"

namespace hmmob {

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
