#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

#include "core/audio/AudioEncoder.h"

namespace {

constexpr int kSampleRate = 48000;
constexpr int kChannels = 2;
constexpr int kBitrate = 192000;
constexpr int kPcmFrameSize = 1024;

const hmmob::AudioEncoder::PacketCallback kNoop =
    [](const hmmob::AacPacket&) {};

// Reads the little-endian s16 payload back into host samples.
std::vector<int16_t> DecodeS16le(const hmmob::AacPacket& packet) {
    REQUIRE(packet.data.size() % 2 == 0);
    std::vector<int16_t> samples(packet.data.size() / 2);
    for (size_t i = 0; i < samples.size(); ++i) {
        samples[i] = static_cast<int16_t>(
            static_cast<uint16_t>(packet.data[i * 2]) |
            (static_cast<uint16_t>(packet.data[i * 2 + 1]) << 8));
    }
    return samples;
}

std::vector<hmmob::AacPacket> CollectPcm(hmmob::AudioEncoder& encoder,
                                         const std::vector<float>& input,
                                         int frames) {
    std::vector<hmmob::AacPacket> packets;
    const auto collect = [&](const hmmob::AacPacket& packet) {
        packets.push_back(packet);
    };
    REQUIRE(encoder.Encode(input.data(), frames, collect));
    REQUIRE(encoder.Flush(collect));
    return packets;
}

}  // namespace

TEST_CASE("AudioEncoder PCM converts float to s16le with clipping and rounding") {
    hmmob::AudioEncoder encoder;
    REQUIRE(encoder.Initialize(kSampleRate, kChannels, kBitrate,
                               hmmob::AudioCodec::PcmS16Le));
    CHECK(encoder.codec() == hmmob::AudioCodec::PcmS16Le);
    CHECK(encoder.sample_rate() == kSampleRate);
    CHECK(encoder.channels() == kChannels);
    CHECK(encoder.bitrate() == kSampleRate * kChannels * 16);
    CHECK(encoder.frame_size() == kPcmFrameSize);
    CHECK(encoder.initial_padding_samples() == 0);
    CHECK(encoder.extradata().empty());

    // Four stereo frames, with left/right deliberately different so a channel
    // swap or a mono collapse would be visible.
    const std::vector<float> input = {
        0.0f,  1.0f,    // full-scale positive
        -1.0f, 2.0f,    // full-scale negative, clipped positive
        0.5f,  -2.0f,   // rounded half, clipped negative
        0.25f, -0.25f,  // fractional rounding
    };
    const std::vector<hmmob::AacPacket> packets =
        CollectPcm(encoder, input, 4);
    REQUIRE(packets.size() == 1);
    CHECK(packets[0].pts_samples == 0);
    CHECK(packets[0].duration_samples == 4);
    CHECK(packets[0].data.size() == 4 * kChannels * 2);

    const std::vector<int16_t> samples = DecodeS16le(packets[0]);
    REQUIRE(samples.size() == input.size());
    CHECK(samples[0] == 0);
    CHECK(samples[1] == 32767);
    CHECK(samples[2] == -32767);
    CHECK(samples[3] == 32767);   // 2.0 clamped to 1.0
    CHECK(samples[4] == 16384);   // 0.5 * 32767 = 16383.5, rounded away
    CHECK(samples[5] == -32767);  // -2.0 clamped to -1.0
    CHECK(samples[6] == 8192);    // 0.25 * 32767 = 8191.75
    CHECK(samples[7] == -8192);
}

TEST_CASE("AudioEncoder PCM emits 1024-frame packets with sample PTS") {
    hmmob::AudioEncoder encoder;
    // The PCM bitrate is derived; the argument is ignored.
    REQUIRE(encoder.Initialize(44100, 1, 0, hmmob::AudioCodec::PcmS16Le));

    std::vector<hmmob::AacPacket> packets;
    const auto collect = [&](const hmmob::AacPacket& packet) {
        packets.push_back(packet);
    };

    constexpr int kFrames = 3000;
    std::vector<float> input(kFrames, 0.5f);
    REQUIRE(encoder.Encode(input.data(), kFrames, collect));
    // 3000 frames = two full 1024-frame packets; the tail waits for Flush.
    REQUIRE(packets.size() == 2);
    CHECK(packets[0].pts_samples == 0);
    CHECK(packets[1].pts_samples == 1024);
    CHECK(packets[0].duration_samples == kPcmFrameSize);
    CHECK(packets[1].duration_samples == kPcmFrameSize);
    CHECK(packets[0].data.size() == kPcmFrameSize * 2);
    CHECK(packets[1].data.size() == kPcmFrameSize * 2);

    REQUIRE(encoder.Flush(collect));
    REQUIRE(packets.size() == 3);
    CHECK(packets[2].pts_samples == 2048);
    CHECK(packets[2].duration_samples == 3000 - 2048);
    CHECK(packets[2].data.size() == static_cast<size_t>(3000 - 2048) * 2);
    CHECK(DecodeS16le(packets[2])[0] == 16384);

    // Every input frame was emitted exactly once.
    int64_t total = 0;
    for (const auto& packet : packets) {
        total += packet.duration_samples;
    }
    CHECK(total == kFrames);
}

TEST_CASE("AudioEncoder PCM handles irregular chunks and flush semantics") {
    hmmob::AudioEncoder encoder;
    REQUIRE(encoder.Initialize(kSampleRate, kChannels, kBitrate,
                               hmmob::AudioCodec::PcmS16Le));

    std::vector<hmmob::AacPacket> packets;
    const auto collect = [&](const hmmob::AacPacket& packet) {
        packets.push_back(packet);
    };

    // Empty input is a benign no-op.
    REQUIRE(encoder.Encode(nullptr, 0, collect));
    const std::vector<float> chunk(333 * kChannels, 0.1f);
    REQUIRE(encoder.Encode(chunk.data(), 333, collect));
    CHECK(packets.empty());
    REQUIRE(encoder.Encode(chunk.data(), 333, collect));
    CHECK(packets.empty());
    REQUIRE(encoder.Encode(chunk.data(), 333, collect));
    CHECK(packets.empty());
    REQUIRE(encoder.Encode(chunk.data(), 333, collect));
    // 1332 frames: one full packet, 308 frames pending.
    REQUIRE(packets.size() == 1);
    CHECK(packets[0].pts_samples == 0);
    CHECK(packets[0].duration_samples == kPcmFrameSize);

    REQUIRE(encoder.Flush(collect));
    REQUIRE(packets.size() == 2);
    CHECK(packets[1].pts_samples == kPcmFrameSize);
    CHECK(packets[1].duration_samples == 1332 - kPcmFrameSize);

    // Flush is one-shot and Encode is rejected afterwards.
    REQUIRE_FALSE(encoder.Flush(collect));
    REQUIRE_FALSE(encoder.Encode(chunk.data(), 333, collect));
    REQUIRE_FALSE(encoder.Encode(nullptr, 10, collect));
}

TEST_CASE("AudioEncoder rejects unsupported PCM configuration") {
    hmmob::AudioEncoder encoder;
    for (const int rate : {0, 8000, 22050, 32000, 96000}) {
        INFO("sample rate " << rate);
        CHECK_FALSE(encoder.Initialize(rate, kChannels, kBitrate,
                                       hmmob::AudioCodec::PcmS16Le));
    }
    for (const int channels : {0, 3, 8}) {
        INFO("channels " << channels);
        CHECK_FALSE(encoder.Initialize(kSampleRate, channels, kBitrate,
                                       hmmob::AudioCodec::PcmS16Le));
    }
    CHECK_FALSE(encoder.is_initialized());
    CHECK(encoder.sample_rate() == 0);
    CHECK(encoder.channels() == 0);
    CHECK(encoder.bitrate() == 0);
    CHECK(encoder.frame_size() == 0);
    CHECK(encoder.initial_padding_samples() == 0);
    CHECK(encoder.extradata().empty());

    const std::vector<float> pcm(static_cast<size_t>(kSampleRate) * kChannels,
                                 0.0f);
    CHECK_FALSE(encoder.Encode(pcm.data(), kSampleRate, kNoop));
    CHECK_FALSE(encoder.Flush(kNoop));
}

TEST_CASE("AudioEncoder AAC backend exposes encoder settings") {
    hmmob::AudioEncoder encoder;
    REQUIRE(encoder.Initialize(kSampleRate, kChannels, kBitrate,
                               hmmob::AudioCodec::Aac));
    CHECK(encoder.codec() == hmmob::AudioCodec::Aac);
    CHECK(encoder.is_initialized());
    CHECK(encoder.sample_rate() == kSampleRate);
    CHECK(encoder.channels() == kChannels);
    CHECK(encoder.bitrate() == kBitrate);
    CHECK(encoder.frame_size() == 1024);
    CHECK(encoder.initial_padding_samples() == 1024);
    REQUIRE(encoder.extradata().size() == 5);

    std::vector<hmmob::AacPacket> packets;
    const auto collect = [&](const hmmob::AacPacket& packet) {
        packets.push_back(packet);
    };
    std::vector<float> pcm(static_cast<size_t>(kSampleRate) * kChannels, 0.25f);
    REQUIRE(encoder.Encode(pcm.data(), kSampleRate, collect));
    REQUIRE(encoder.Flush(collect));
    REQUIRE_FALSE(packets.empty());
    CHECK(packets.front().pts_samples == -1024);
    CHECK_FALSE(packets.front().data.empty());

    // AAC bitrates outside the supported range are rejected.
    CHECK_FALSE(encoder.Initialize(kSampleRate, kChannels, 64000,
                                   hmmob::AudioCodec::Aac));
    CHECK_FALSE(encoder.Initialize(kSampleRate, kChannels, 400000,
                                   hmmob::AudioCodec::Aac));
    CHECK(encoder.Initialize(kSampleRate, kChannels, 96000,
                             hmmob::AudioCodec::Aac));
    CHECK(encoder.Initialize(kSampleRate, kChannels, 320000,
                             hmmob::AudioCodec::Aac));
}
