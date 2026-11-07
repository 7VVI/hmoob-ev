#include <catch2/catch_test_macros.hpp>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/channel_layout.h>
#include <libavutil/frame.h>
}

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

#include "core/audio/AacEncoder.h"

namespace {

constexpr int kSampleRate = 48000;
constexpr int kChannels = 2;
constexpr int kBitrate = 192000;

const hmmob::AacEncoder::PacketCallback kNoop = [](const hmmob::AacPacket&) {};

void FillStereoSine(std::vector<float>& buffer, int frames, int start_frame,
                    float left_hz, float right_hz) {
    buffer.resize(static_cast<size_t>(frames) * kChannels);
    for (int i = 0; i < frames; ++i) {
        const double t = static_cast<double>(start_frame + i) / kSampleRate;
        buffer[static_cast<size_t>(i) * kChannels + 0] =
            static_cast<float>(std::sin(2.0 * 3.14159265358979323846 * left_hz * t));
        buffer[static_cast<size_t>(i) * kChannels + 1] =
            static_cast<float>(std::sin(2.0 * 3.14159265358979323846 * right_hz * t));
    }
}

// Goertzel energy of one channel at `hz`, used to prove L/R content survives.
double GoertzelEnergy(const std::vector<float>& samples, double hz) {
    const double w = 2.0 * 3.14159265358979323846 * hz / kSampleRate;
    const double coeff = 2.0 * std::cos(w);
    double s1 = 0.0;
    double s2 = 0.0;
    for (const float sample : samples) {
        const double s0 = static_cast<double>(sample) + coeff * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    return s1 * s1 + s2 * s2 - coeff * s1 * s2;
}

}  // namespace

TEST_CASE("AacEncoder encodes 1 second of stereo sine") {
    constexpr int kChunkSampleRate = 48000;
    constexpr int kChunkChannels = 2;
    hmmob::AacEncoder encoder;
    REQUIRE(encoder.Initialize(kChunkSampleRate, kChunkChannels, 192000));
    REQUIRE_FALSE(encoder.extradata().empty());

    std::vector<float> pcm(kChunkSampleRate * kChunkChannels);
    for (int i = 0; i < kChunkSampleRate; ++i) {
        const float s = std::sin(2.0f * 3.14159265f * 440.0f * i / kChunkSampleRate);
        pcm[i * 2 + 0] = s;
        pcm[i * 2 + 1] = s;
    }

    int packets = 0;
    size_t bytes = 0;
    REQUIRE(encoder.Encode(pcm.data(), kChunkSampleRate, [&](const hmmob::AacPacket& p) {
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

TEST_CASE("AacEncoder handles irregular chunks with stable PTS and durations") {
    hmmob::AacEncoder encoder;
    REQUIRE(encoder.Initialize(kSampleRate, kChannels, kBitrate));

    const int padding = encoder.initial_padding_samples();
    REQUIRE(padding == 1024);

    std::vector<hmmob::AacPacket> packets;
    const auto collect = [&](const hmmob::AacPacket& p) { packets.push_back(p); };

    const int chunk_sizes[] = {1, 333, 4096, 48000, 2000};
    int total_frames = 0;
    std::vector<float> chunk;
    for (const int frames : chunk_sizes) {
        FillStereoSine(chunk, frames, total_frames, 440.0f, 880.0f);
        REQUIRE(encoder.Encode(chunk.data(), frames, collect));
        total_frames += frames;
    }
    // Empty input is a benign no-op, even with a null pointer.
    REQUIRE(encoder.Encode(nullptr, 0, collect));
    REQUIRE(encoder.Flush(collect));

    REQUIRE_FALSE(packets.empty());
    REQUIRE(packets.front().pts_samples == -padding);

    int64_t expected_pts = -padding;
    int64_t total_duration = 0;
    for (size_t i = 0; i < packets.size(); ++i) {
        INFO("packet " << i << " pts=" << packets[i].pts_samples
                       << " duration=" << packets[i].duration_samples);
        REQUIRE(packets[i].pts_samples == expected_pts);
        REQUIRE(packets[i].duration_samples > 0);
        REQUIRE(packets[i].duration_samples <= 1024);
        if (i + 1 < packets.size()) {
            REQUIRE(packets[i].duration_samples == 1024);
        }
        REQUIRE_FALSE(packets[i].data.empty());
        expected_pts += packets[i].duration_samples;
        total_duration += packets[i].duration_samples;
    }
    // Encoded output must cover the input plus the priming delay.
    REQUIRE(total_duration >= total_frames);
}

TEST_CASE("AacEncoder writes AAC-LC AudioSpecificConfig for 48 kHz stereo") {
    hmmob::AacEncoder encoder;
    REQUIRE(encoder.Initialize(kSampleRate, kChannels, kBitrate));

    const std::vector<uint8_t>& asc = encoder.extradata();
    REQUIRE(asc.size() == 5);
    const int audio_object_type = asc[0] >> 3;
    const int sample_rate_index = ((asc[0] & 0x07) << 1) | (asc[1] >> 7);
    const int channel_config = (asc[1] >> 3) & 0x0F;
    REQUIRE(audio_object_type == 2);   // AAC-LC
    REQUIRE(sample_rate_index == 3);   // 48000 Hz
    REQUIRE(channel_config == 2);      // stereo
}

TEST_CASE("AacEncoder output decodes to stereo with intact channels") {
    hmmob::AacEncoder encoder;
    REQUIRE(encoder.Initialize(kSampleRate, kChannels, kBitrate));
    const int padding = encoder.initial_padding_samples();

    std::vector<hmmob::AacPacket> packets;
    const auto collect = [&](const hmmob::AacPacket& p) { packets.push_back(p); };

    constexpr int kTotalFrames = 48000;
    constexpr int kChunkFrames = 4096;
    std::vector<float> chunk;
    int encoded_frames = 0;
    while (encoded_frames < kTotalFrames) {
        const int frames = std::min(kChunkFrames, kTotalFrames - encoded_frames);
        FillStereoSine(chunk, frames, encoded_frames, 440.0f, 880.0f);
        REQUIRE(encoder.Encode(chunk.data(), frames, collect));
        encoded_frames += frames;
    }
    REQUIRE(encoder.Flush(collect));
    REQUIRE_FALSE(packets.empty());

    const AVCodec* decoder = avcodec_find_decoder(AV_CODEC_ID_AAC);
    REQUIRE(decoder != nullptr);
    AVCodecContext* dec = avcodec_alloc_context3(decoder);
    REQUIRE(dec != nullptr);
    dec->extradata = static_cast<uint8_t*>(
        av_mallocz(encoder.extradata().size() + AV_INPUT_BUFFER_PADDING_SIZE));
    REQUIRE(dec->extradata != nullptr);
    std::memcpy(dec->extradata, encoder.extradata().data(), encoder.extradata().size());
    dec->extradata_size = static_cast<int>(encoder.extradata().size());
    REQUIRE(avcodec_open2(dec, decoder, nullptr) >= 0);

    std::vector<float> left;
    std::vector<float> right;
    AVFrame* frame = av_frame_alloc();
    REQUIRE(frame != nullptr);
    const auto drain = [&] {
        for (;;) {
            const int result = avcodec_receive_frame(dec, frame);
            if (result == AVERROR(EAGAIN) || result == AVERROR_EOF) {
                return;
            }
            REQUIRE(result >= 0);
            REQUIRE(frame->ch_layout.nb_channels == kChannels);
            const float* l = reinterpret_cast<const float*>(frame->data[0]);
            const float* r = reinterpret_cast<const float*>(frame->data[1]);
            left.insert(left.end(), l, l + frame->nb_samples);
            right.insert(right.end(), r, r + frame->nb_samples);
            av_frame_unref(frame);
        }
    };

    for (const auto& packet : packets) {
        AVPacket* pkt = av_packet_alloc();
        REQUIRE(pkt != nullptr);
        REQUIRE(av_new_packet(pkt, static_cast<int>(packet.data.size())) >= 0);
        std::memcpy(pkt->data, packet.data.data(), packet.data.size());
        const int sent = avcodec_send_packet(dec, pkt);
        av_packet_free(&pkt);
        REQUIRE(sent >= 0);
        drain();
    }
    REQUIRE(avcodec_send_packet(dec, nullptr) >= 0);
    drain();
    av_frame_free(&frame);
    avcodec_free_context(&dec);

    REQUIRE(left.size() == right.size());
    REQUIRE(left.size() >= static_cast<size_t>(kTotalFrames));
    REQUIRE(left.size() <=
            static_cast<size_t>(kTotalFrames) + 2 * static_cast<size_t>(padding));

    // Distinct tones per channel: a swap or channel collapse is visible here.
    const double left_440 = GoertzelEnergy(left, 440.0);
    const double right_440 = GoertzelEnergy(right, 440.0);
    const double left_880 = GoertzelEnergy(left, 880.0);
    const double right_880 = GoertzelEnergy(right, 880.0);
    REQUIRE(left_440 > 10.0 * right_440);
    REQUIRE(right_880 > 10.0 * left_880);
}

TEST_CASE("AacEncoder rejects invalid configuration") {
    hmmob::AacEncoder encoder;
    REQUIRE_FALSE(encoder.Initialize(0, kChannels, kBitrate));
    REQUIRE_FALSE(encoder.Initialize(kSampleRate, 0, kBitrate));
    REQUIRE_FALSE(encoder.Initialize(kSampleRate, kChannels, 0));
    REQUIRE_FALSE(encoder.is_initialized());
    REQUIRE(encoder.initial_padding_samples() == 0);
    REQUIRE(encoder.extradata().empty());

    std::vector<float> pcm(static_cast<size_t>(kSampleRate) * kChannels, 0.0f);
    REQUIRE_FALSE(encoder.Encode(pcm.data(), kSampleRate, kNoop));
    REQUIRE_FALSE(encoder.Flush(kNoop));
}

TEST_CASE("AacEncoder rejects Encode after Flush") {
    hmmob::AacEncoder encoder;
    REQUIRE(encoder.Initialize(kSampleRate, kChannels, kBitrate));

    std::vector<float> pcm(static_cast<size_t>(kSampleRate) * kChannels, 0.25f);
    REQUIRE(encoder.Encode(pcm.data(), kSampleRate, kNoop));
    REQUIRE(encoder.Flush(kNoop));
    REQUIRE_FALSE(encoder.Encode(pcm.data(), 1024, kNoop));
    REQUIRE_FALSE(encoder.Flush(kNoop));
}
