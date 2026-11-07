#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

namespace hmmob {

// Writes an MP4 from Annex B H.264/HEVC access units. The MP4 header is
// deferred to the first packet so the muxer can derive avcC/hvcC extradata
// from that packet's SPS/PPS (H.264) or VPS/SPS/PPS (HEVC).
class FfmpegMuxer {
public:
    FfmpegMuxer() = default;
    ~FfmpegMuxer();
    FfmpegMuxer(const FfmpegMuxer&) = delete;
    FfmpegMuxer& operator=(const FfmpegMuxer&) = delete;

    bool Open(const std::string& path, const std::string& codec,
              int width, int height, int fps, int bitrate);
    bool WritePacket(const uint8_t* data, size_t size, int64_t pts_us, bool keyframe);
    bool Close();

private:
    struct Impl;
    Impl* impl_ = nullptr;
};

}  // namespace hmmob
