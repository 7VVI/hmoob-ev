#pragma once
#include <cstdint>
#include <functional>
#include <vector>

#include "core/audio/AacEncoder.h"
#include "core/audio/AudioCodec.h"

namespace hmmob {

// Encodes interleaved float32 PCM into the configured audio format.
//
// Both backends consume the same callback packet type (AacPacket): `pts_samples`
// is the presentation timestamp in samples under time_base = 1/sample_rate and
// `duration_samples` the decoded sample count.
//
//  - AudioCodec::Aac delegates to AacEncoder (AAC-LC, 44.1/48 kHz, 1-2
//    channels, 96-320 kbps). The first packet carries a negative PTS equal to
//    -initial_padding_samples() (encoder priming).
//  - AudioCodec::PcmS16Le converts float [-1, 1] to interleaved signed 16-bit
//    little-endian (clamped and rounded), buffers it and emits 1024-frame
//    packets. PTS starts at 0 and advances by the sample count; Flush emits a
//    short remainder packet. There is no extradata and no initial padding.
//
// Stateful and not thread-safe: Initialize, Encode, Flush and Shutdown must be
// called from a single thread. Encode accepts arbitrary chunk sizes. Flush
// emits the delayed tail (padding it to a full frame when the AAC encoder
// requires it) and drains the backend; Encode and Flush return false afterwards
// until Initialize is called again. The packet passed to a callback is only
// valid during that call.
//
// Configuration is validated strictly: sample_rate must be 44100 or 48000 and
// channels 1 or 2. AAC additionally requires a 96000..320000 bps bitrate; the
// PCM bitrate is derived from the sample format and the argument is ignored.
class AudioEncoder {
public:
    using PacketCallback = std::function<void(const AacPacket&)>;

    AudioEncoder() = default;
    ~AudioEncoder();
    AudioEncoder(const AudioEncoder&) = delete;
    AudioEncoder& operator=(const AudioEncoder&) = delete;

    [[nodiscard]] bool Initialize(int sample_rate, int channels, int bitrate,
                                  AudioCodec codec);
    [[nodiscard]] bool Encode(const float* interleaved, int frames,
                              const PacketCallback& callback);
    [[nodiscard]] bool Flush(const PacketCallback& callback);
    void Shutdown();
    [[nodiscard]] bool is_initialized() const;
    // Configured stream parameters; 0 when uninitialized. Use these to fill
    // FfmpegMuxer::AudioConfig so the container matches the encoded stream.
    [[nodiscard]] int sample_rate() const;
    [[nodiscard]] int channels() const;
    // AAC: configured bitrate. PCM: derived 16-bit rate (rate * channels * 16).
    [[nodiscard]] int bitrate() const;
    // Packet frame size in samples; 0 when uninitialized.
    [[nodiscard]] int frame_size() const;
    // Encoder delay in samples; 0 for PCM or when uninitialized.
    [[nodiscard]] int initial_padding_samples() const;
    // AudioSpecificConfig for AAC; empty for PCM.
    [[nodiscard]] const std::vector<uint8_t>& extradata() const;
    // Selected format; AudioCodec::Aac when uninitialized.
    [[nodiscard]] AudioCodec codec() const;

private:
    struct Impl;
    Impl* impl_ = nullptr;
};

}  // namespace hmmob
