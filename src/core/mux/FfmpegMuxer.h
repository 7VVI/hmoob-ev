#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

namespace hmmob {

// Writes an MP4 from Annex B H.264/HEVC access units. The MP4 header is
// deferred to the first packet so the muxer can derive avcC/hvcC extradata
// from that packet's SPS/PPS (H.264) or VPS/SPS/PPS (HEVC).
//
// Contract:
//  - The first packet must be a keyframe carrying SPS/PPS (H.264) or
//    VPS/SPS/PPS (HEVC); otherwise the session fails and no further packet
//    is written.
//  - WritePacket returns false when the packet was not written. Once a write
//    fails the muxer stays failed: later calls return false until Close.
//  - Close returns false if no header was ever written (nothing was muxed)
//    or if a write failed. It still releases all resources and is idempotent;
//    a default-constructed or already-closed muxer returns true.
class FfmpegMuxer {
public:
    FfmpegMuxer() = default;
    ~FfmpegMuxer();
    FfmpegMuxer(const FfmpegMuxer&) = delete;
    FfmpegMuxer& operator=(const FfmpegMuxer&) = delete;

    // Opens `path` for writing. `codec` must be "hevc" or "h264". The file is
    // created immediately, but the container header is not written until the
    // first WritePacket supplies the codec extradata.
    bool Open(const std::string& path, const std::string& codec,
              int width, int height, int fps, int bitrate);
    // Writes one Annex B access unit. See the class contract above.
    bool WritePacket(const uint8_t* data, size_t size, int64_t pts_us, bool keyframe);
    // Finalizes and releases the file. See the class contract above.
    bool Close();

private:
    struct Impl;
    Impl* impl_ = nullptr;
};

}  // namespace hmmob
