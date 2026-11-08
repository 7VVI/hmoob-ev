#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "core/audio/AudioCodec.h"

namespace hmmob {

// Audio stream settings for FfmpegMuxer.
//
//  - AudioCodec::Aac: `extradata` must be the AudioSpecificConfig produced by
//    AacEncoder and `initial_padding` its encoder priming delay in samples. In
//    FFmpeg 9 `codecpar->initial_padding` is metadata only: movenc derives the
//    MP4 edit list from the negative first DTS, so the muxer must keep the
//    first packet's negative PTS rather than clamp it.
//  - AudioCodec::PcmS16Le: raw interleaved signed 16-bit little-endian samples.
//    No extradata, no initial padding; the first packet starts at PTS 0.
//
// `sample_rate` must be 44100 or 48000 and `channels` 1 or 2. `frame_size` is
// the encoder's packet length (1024 for both backends). The PCM bitrate is
// derived from the sample format; the `bitrate` field is used for AAC only.
struct AudioConfig {
    bool enabled = false;
    AudioCodec codec = AudioCodec::Aac;
    int sample_rate = 48000;
    int channels = 2;
    int bitrate = 192000;
    int frame_size = 1024;
    int initial_padding = 0;
    std::vector<uint8_t> extradata;
};

// Writes an MP4, MOV, Matroska, AVI, FLV or ASF/WMV container from Annex B
// H.264/HEVC access units, optionally with an AAC or PCM audio track. The
// container is chosen by the output path's extension. The container header is
// deferred to the first video packet so the muxer can derive the codec
// extradata from that packet's SPS/PPS (H.264) or VPS/SPS/PPS (HEVC); AAC
// extradata is supplied through AudioConfig.
//
// Container support (unsupported combinations fail Open with a diagnostic):
//  - .mp4/.mov: H.264 or HEVC, AAC or PCM.
//  - .mkv:      H.264 or HEVC, AAC or PCM.
//  - .avi/.wmv: H.264 only, AAC or PCM.
//  - .flv:      H.264 only, AAC only.
//
// Contract:
//  - The first video packet must be a keyframe carrying SPS/PPS (H.264) or
//    VPS/SPS/PPS (HEVC); otherwise the session fails and no further packet
//    is written.
//  - MP4/MOV/MKV/FLV store video as 4-byte length-prefixed NAL units with
//    avcC/hvcC extradata. AVI and ASF store the original Annex B access units
//    with Annex B parameter-set extradata, because avienc rejects AVC-length-
//    prefixed samples for H.264.
//  - Audio packets written before the video header exists are buffered (at
//    most 5 seconds by timestamp, sample count, packet count or byte count;
//    exceeding any limit latches a failure) and flushed in order once the
//    header is written. A negative first PTS (AAC priming) is preserved; PCM
//    packets carry the cumulative sample count instead.
//  - WritePacket and WriteAudioPacket return false when the packet was not
//    written. Once a write fails the muxer stays failed: later calls return
//    false until Close.
//  - Close returns false if no header was ever written (nothing was muxed,
//    including when only audio was buffered) or if a write failed. The file
//    created by Open is left in place (empty when nothing was muxed). Close
//    still releases all resources and is idempotent; a default-constructed or
//    already-closed muxer returns true.
//
// Not thread-safe: Open, WritePacket, WriteAudioPacket and Close must be
// called from a single thread.
class FfmpegMuxer {
public:
    FfmpegMuxer() = default;
    ~FfmpegMuxer();
    FfmpegMuxer(const FfmpegMuxer&) = delete;
    FfmpegMuxer& operator=(const FfmpegMuxer&) = delete;

    // Opens `path` for writing, selecting the container from the file
    // extension. `codec` must be "hevc" or "h264". The file is created
    // immediately, but the container header is not written until the first
    // WritePacket supplies the codec extradata. When `audio.enabled` is true,
    // an AAC or PCM track is declared from `audio`.
    [[nodiscard]] bool Open(const std::string& path, const std::string& codec,
                            int width, int height, int fps, int bitrate,
                            const AudioConfig& audio = {});
    // Writes one Annex B access unit. See the class contract above.
    [[nodiscard]] bool WritePacket(const uint8_t* data, size_t size,
                                   int64_t pts_us, bool keyframe);
    // Writes one encoded audio access unit. `pts_us` is the presentation
    // timestamp in microseconds and `duration_samples` the decoded sample
    // count, both from the audio packet. A negative first PTS (AAC priming) is
    // preserved, not clamped; PCM packets start at PTS 0.
    [[nodiscard]] bool WriteAudioPacket(const uint8_t* data, size_t size,
                                        int64_t pts_us, int duration_samples);
    // Finalizes and releases the file. See the class contract above.
    [[nodiscard]] bool Close();

private:
    struct Impl;
    Impl* impl_ = nullptr;
};

}  // namespace hmmob
