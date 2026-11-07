#pragma once
#include <cstdint>
#include <functional>
#include <vector>

namespace hmmob {

struct AacPacket {
    std::vector<uint8_t> data;
    int64_t pts_samples = 0;
};

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
    // AudioSpecificConfig; copy into the MP4 audio codecpar before header.
    [[nodiscard]] const std::vector<uint8_t>& extradata() const;

private:
    struct Impl;
    Impl* impl_ = nullptr;
};

}  // namespace hmmob
