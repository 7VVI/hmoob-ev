#pragma once
#include <cstdint>
#include <functional>
#include <vector>

namespace hmmob {

struct AacPacket {
    std::vector<uint8_t> data;
    // Presentation timestamp in samples under time_base = 1/sample_rate.
    // The first packet PTS is -initial_padding_samples() (negative encoder
    // priming); later packets advance by the previous packet's duration.
    int64_t pts_samples = 0;
    // Decoded samples this packet produces (normally 1024). The final packet
    // may be shorter when the encoder accepts a small last frame.
    int duration_samples = 0;
};

// Encodes interleaved float32 PCM into raw AAC-LC access units with FFmpeg.
//
// Stateful and not thread-safe: Initialize, Encode, Flush and Shutdown must be
// called from a single thread. Encode accepts arbitrary chunk sizes and buffers
// samples until a full encoder frame is available. Flush emits the delayed tail
// (padding it to a full frame when the encoder requires it) and drains the
// encoder; Encode and Flush return false afterwards until Initialize is called
// again. The AacPacket passed to a callback is only valid during that call.
//
// The first packet carries negative PTS equal to -initial_padding_samples()
// (1024 with FFmpeg's native AAC encoder). The muxer must offset the audio
// timeline by that priming delay, typically via the MP4 edit list.
class AacEncoder {
public:
    using PacketCallback = std::function<void(const AacPacket&)>;

    AacEncoder() = default;
    ~AacEncoder();
    AacEncoder(const AacEncoder&) = delete;
    AacEncoder& operator=(const AacEncoder&) = delete;

    [[nodiscard]] bool Initialize(int sample_rate, int channels, int bitrate);
    [[nodiscard]] bool Encode(const float* interleaved, int frames,
                              const PacketCallback& callback);
    [[nodiscard]] bool Flush(const PacketCallback& callback);
    void Shutdown();
    [[nodiscard]] bool is_initialized() const;
    // Encoder delay in samples; 0 when uninitialized or unsupported.
    [[nodiscard]] int initial_padding_samples() const;
    // AudioSpecificConfig; copy into the MP4 audio codecpar before header.
    [[nodiscard]] const std::vector<uint8_t>& extradata() const;

private:
    struct Impl;
    Impl* impl_ = nullptr;
};

}  // namespace hmmob
