#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace hmmob {

// AAC stream settings for FfmpegMuxer. `extradata` is the AudioSpecificConfig
// produced by AacEncoder; `initial_padding` is its encoder priming delay in
// samples. The muxer records the priming delay in the track metadata and must
// keep the first packet's negative PTS: FFmpeg's movenc turns that into the
// MP4 edit list that removes the priming from the presentation timeline.
struct AudioConfig {
    bool enabled = false;
    int sample_rate = 48000;
    int channels = 2;
    int bitrate = 192000;
    int frame_size = 1024;
    int initial_padding = 0;
    std::vector<uint8_t> extradata;
};

// Writes an MP4 from Annex B H.264/HEVC access units, optionally with an AAC
// audio track. The MP4 header is deferred to the first video packet so the
// muxer can derive avcC/hvcC extradata from that packet's SPS/PPS (H.264) or
// VPS/SPS/PPS (HEVC); AAC extradata is supplied through AudioConfig.
//
// Contract:
//  - The first video packet must be a keyframe carrying SPS/PPS (H.264) or
//    VPS/SPS/PPS (HEVC); otherwise the session fails and no further packet
//    is written.
//  - Audio packets written before the video header exists are buffered (at
//    most 5 seconds; exceeding that latches a failure) and flushed in order
//    once the header is written.
//  - WritePacket and WriteAudioPacket return false when the packet was not
//    written. Once a write fails the muxer stays failed: later calls return
//    false until Close.
//  - Close returns false if no header was ever written (nothing was muxed,
//    including when only audio was buffered) or if a write failed. The file
//    created by Open is left in place (empty when nothing was muxed). Close
//    still releases all resources and is idempotent; a default-constructed or
//    already-closed muxer returns true.
class FfmpegMuxer {
public:
    FfmpegMuxer() = default;
    ~FfmpegMuxer();
    FfmpegMuxer(const FfmpegMuxer&) = delete;
    FfmpegMuxer& operator=(const FfmpegMuxer&) = delete;

    // Opens `path` for writing. `codec` must be "hevc" or "h264". The file is
    // created immediately, but the container header is not written until the
    // first WritePacket supplies the codec extradata. When `audio.enabled` is
    // true, an AAC track is declared from `audio.extradata`.
    bool Open(const std::string& path, const std::string& codec,
              int width, int height, int fps, int bitrate,
              const AudioConfig& audio = {});
    // Writes one Annex B access unit. See the class contract above.
    bool WritePacket(const uint8_t* data, size_t size, int64_t pts_us, bool keyframe);
    // Writes one AAC access unit. `pts_us` is the presentation timestamp in
    // microseconds and `duration_samples` the decoded sample count, both from
    // AacPacket. Negative first PTS (AAC priming) is preserved, not clamped.
    bool WriteAudioPacket(const uint8_t* data, size_t size, int64_t pts_us,
                          int duration_samples);
    // Finalizes and releases the file. See the class contract above.
    bool Close();

private:
    struct Impl;
    Impl* impl_ = nullptr;
};

}  // namespace hmmob
