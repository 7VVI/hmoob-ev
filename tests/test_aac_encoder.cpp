#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <vector>
#include "core/audio/AacEncoder.h"

TEST_CASE("AacEncoder encodes 1 second of stereo sine") {
    constexpr int kSampleRate = 48000;
    constexpr int kChannels = 2;
    hmmob::AacEncoder encoder;
    REQUIRE(encoder.Initialize(kSampleRate, kChannels, 192000));
    REQUIRE_FALSE(encoder.extradata().empty());

    std::vector<float> pcm(kSampleRate * kChannels);
    for (int i = 0; i < kSampleRate; ++i) {
        const float s = std::sin(2.0f * 3.14159265f * 440.0f * i / kSampleRate);
        pcm[i * 2 + 0] = s;
        pcm[i * 2 + 1] = s;
    }

    int packets = 0;
    size_t bytes = 0;
    REQUIRE(encoder.Encode(pcm.data(), kSampleRate, [&](const hmmob::AacPacket& p) {
        ++packets;
        bytes += p.data.size();
    }));
    REQUIRE(encoder.Flush([&](const hmmob::AacPacket& p) {
        ++packets;
        bytes += p.data.size();
    }));
    REQUIRE(packets > 0);
    REQUIRE(bytes > 0);
}
