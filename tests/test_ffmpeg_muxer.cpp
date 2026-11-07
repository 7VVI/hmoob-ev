#include <catch2/catch_test_macros.hpp>
#include <d3d11.h>
#include <process.h>
#include <wrl/client.h>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/macros.h>
#include <libavutil/mathematics.h>
#include <libavutil/pixfmt.h>
}

#include "core/audio/AacEncoder.h"
#include "core/encoder/AmfEncoder.h"
#include "core/gfx/D3D11Device.h"
#include "core/mux/FfmpegMuxer.h"

namespace {

using Bytes = std::vector<uint8_t>;

constexpr int kWidth = 640;
constexpr int kHeight = 360;
constexpr int kFps = 30;
constexpr int kBitrate = 4'000'000;
constexpr int kFrameCount = 30;
constexpr int64_t kFrameUs = 1'000'000 / kFps;

// Removes the output file even when a Catch2 assertion aborts the test.
struct TempFile {
    std::filesystem::path path;

    ~TempFile() {
        std::error_code error;
        std::filesystem::remove(path, error);
    }
};

// A per-run name so concurrent test processes cannot collide.
std::filesystem::path UniqueTempPath(const std::string& tag) {
    static std::atomic<int> counter{0};
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() /
           ("hmmob-muxer-" + tag + "-" + std::to_string(::_getpid()) + "-" +
            std::to_string(stamp) + "-" + std::to_string(counter.fetch_add(1)) +
            ".mp4");
}

// A syntactically valid Annex B H.264 keyframe: SPS, PPS and an IDR slice.
Bytes SyntheticH264Keyframe() {
    return {0x00, 0x00, 0x00, 0x01, 0x67, 0x42, 0x00, 0x1E,
            0x00, 0x00, 0x00, 0x01, 0x68, 0xCE, 0x3C, 0x80,
            0x00, 0x00, 0x00, 0x01, 0x65, 0x88, 0x84};
}

Bytes SyntheticH264InterFrame() {
    return {0x00, 0x00, 0x00, 0x01, 0x41, 0x9A, 0x02, 0x03};
}

// AAC-LC 48 kHz stereo AudioSpecificConfig: base ASC plus the SBR-absent sync
// extension, exactly as FFmpeg's native AAC encoder emits it.
const Bytes kTestAacExtradata = {0x11, 0x90, 0x56, 0xE5, 0x00};

constexpr int kTestAudioSampleRate = 48000;
constexpr int kTestAudioFrameSize = 1024;

hmmob::AudioConfig TestAudioConfig() {
    hmmob::AudioConfig audio;
    audio.enabled = true;
    audio.sample_rate = kTestAudioSampleRate;
    audio.channels = 2;
    audio.bitrate = 192000;
    audio.frame_size = kTestAudioFrameSize;
    audio.initial_padding = kTestAudioFrameSize;
    audio.extradata = kTestAacExtradata;
    return audio;
}

// The muxer copies payloads verbatim, so any bytes stand in for AAC access
// units when the test only inspects container structure.
Bytes DummyAacPacket(uint8_t seed, size_t size = 64) {
    return Bytes(size, seed);
}

int64_t AudioSamplesToUs(int64_t samples) {
    return samples * 1'000'000 / kTestAudioSampleRate;
}

struct MuxRun {
    int packets_written = 0;
    int keyframes = 0;
    int packets_demuxed = 0;
    int frames_decoded = 0;
};

// Encodes kFrameCount frames with AMF, muxes them, then demuxes and decodes
// the resulting file, asserting the full chain along the way.
MuxRun RunMuxAndDemux(const std::string& codec, AVCodecID expected_codec_id) {
    hmmob::D3D11Device d3d;
    if (!d3d.Create()) {
        SKIP("D3D11 device unavailable");
    }

    hmmob::AmfEncoder encoder;
    if (!encoder.Initialize(d3d.device(), codec, kWidth, kHeight, kFps, kBitrate)) {
        SKIP("AMF " << codec << " encoder unavailable");
    }

    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = kWidth;
    desc.Height = kHeight;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_NV12;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
    REQUIRE(SUCCEEDED(d3d.device()->CreateTexture2D(&desc, nullptr, &texture)));

    TempFile output{UniqueTempPath(codec)};
    const std::string path = output.path.string();

    hmmob::FfmpegMuxer muxer;
    REQUIRE(muxer.Open(path, codec, kWidth, kHeight, kFps, kBitrate));

    MuxRun run;
    std::vector<uint8_t> nv12(kWidth * kHeight * 3 / 2, 128);
    auto write_packet = [&](const hmmob::AmfPacket& packet) {
        REQUIRE(!packet.data.empty());
        REQUIRE(muxer.WritePacket(packet.data.data(), packet.data.size(),
                                  packet.pts_us, packet.keyframe));
        ++run.packets_written;
        if (packet.keyframe) {
            ++run.keyframes;
        }
    };

    for (int i = 0; i < kFrameCount; ++i) {
        // Vary the luma so every frame differs.
        nv12[0] = static_cast<uint8_t>((i * 37) & 0xFF);
        nv12[1] = static_cast<uint8_t>((i * 73) & 0xFF);
        d3d.context()->UpdateSubresource(texture.Get(), 0, nullptr, nv12.data(),
                                         kWidth, kWidth * kHeight);
        REQUIRE(encoder.Encode(texture.Get(), i * kFrameUs, write_packet));
    }
    REQUIRE(encoder.Flush(write_packet));
    REQUIRE(muxer.Close());

    // Demux the file the muxer produced.
    AVFormatContext* format = nullptr;
    REQUIRE(avformat_open_input(&format, path.c_str(), nullptr, nullptr) >= 0);
    REQUIRE(format != nullptr);
    REQUIRE(avformat_find_stream_info(format, nullptr) >= 0);

    const int video_index =
        av_find_best_stream(format, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    REQUIRE(video_index >= 0);

    const AVStream* stream = format->streams[video_index];
    CHECK(stream->codecpar->codec_id == expected_codec_id);
    CHECK(stream->codecpar->width == kWidth);
    CHECK(stream->codecpar->height == kHeight);
    CHECK(stream->codecpar->extradata_size > 0);
    const uint32_t expected_tag = expected_codec_id == AV_CODEC_ID_HEVC
                                      ? MKTAG('h', 'v', 'c', '1')
                                      : MKTAG('a', 'v', 'c', '1');
    CHECK(stream->codecpar->codec_tag == expected_tag);

    if (expected_codec_id == AV_CODEC_ID_H264) {
        // AMF's H.264 encoder is configured for High profile, so the avcC
        // record must end with the High-profile extension.
        REQUIRE(stream->codecpar->extradata_size > 4);
        const int size = stream->codecpar->extradata_size;
        const uint8_t* extradata = stream->codecpar->extradata;
        CHECK(extradata[1] == 100);  // profile_idc High
        CHECK(extradata[size - 4] == 0xFD);  // chroma_format_idc = 1
        CHECK(extradata[size - 3] == 0xF8);  // bit_depth_luma_minus8 = 0
        CHECK(extradata[size - 2] == 0xF8);  // bit_depth_chroma_minus8 = 0
        CHECK(extradata[size - 1] == 0x00);  // numOfSequenceParameterSetExt
    }

    const AVCodec* decoder = avcodec_find_decoder(expected_codec_id);
    REQUIRE(decoder != nullptr);
    AVCodecContext* decode_context = avcodec_alloc_context3(decoder);
    REQUIRE(decode_context != nullptr);
    REQUIRE(avcodec_parameters_to_context(decode_context, stream->codecpar) >= 0);
    REQUIRE(avcodec_open2(decode_context, decoder, nullptr) >= 0);

    AVPacket* packet = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();
    REQUIRE(packet != nullptr);
    REQUIRE(frame != nullptr);

    int read_result = 0;
    while ((read_result = av_read_frame(format, packet)) >= 0) {
        if (packet->stream_index != video_index) {
            av_packet_unref(packet);
            continue;
        }
        ++run.packets_demuxed;
        const int sent = avcodec_send_packet(decode_context, packet);
        av_packet_unref(packet);
        REQUIRE(sent >= 0);

        int receive = 0;
        while ((receive = avcodec_receive_frame(decode_context, frame)) >= 0) {
            ++run.frames_decoded;
            if (run.frames_decoded == 1) {
                CHECK(frame->width == kWidth);
                CHECK(frame->height == kHeight);
                CHECK(frame->format == AV_PIX_FMT_YUV420P);
            }
            av_frame_unref(frame);
        }
        // Only "no frame ready" and "end of stream" are expected here.
        REQUIRE((receive == AVERROR(EAGAIN) || receive == AVERROR_EOF));
    }
    REQUIRE(read_result == AVERROR_EOF);

    // Flush the decoder and drain the frames it still holds.
    REQUIRE(avcodec_send_packet(decode_context, nullptr) >= 0);
    int drain = 0;
    while ((drain = avcodec_receive_frame(decode_context, frame)) >= 0) {
        ++run.frames_decoded;
        av_frame_unref(frame);
    }
    REQUIRE((drain == AVERROR_EOF || drain == AVERROR(EAGAIN)));

    av_frame_free(&frame);
    av_packet_free(&packet);
    avcodec_free_context(&decode_context);
    avformat_close_input(&format);
    return run;
}

void CheckRun(const MuxRun& run) {
    CHECK(run.keyframes >= 1);
    CHECK(run.packets_written >= kFrameCount);
    CHECK(run.packets_demuxed >= kFrameCount);
    CHECK(run.frames_decoded >= kFrameCount);
}

}  // namespace

TEST_CASE("FFmpeg muxer writes a decodable HEVC MP4", "[integration][amf]") {
    CheckRun(RunMuxAndDemux("hevc", AV_CODEC_ID_HEVC));
}

TEST_CASE("FFmpeg muxer writes a decodable H.264 MP4", "[integration][amf]") {
    CheckRun(RunMuxAndDemux("h264", AV_CODEC_ID_H264));
}

TEST_CASE("FFmpeg muxer writes HEVC+AAC MP4", "[integration][amf]") {
    constexpr int kAudioSampleRate = 48000;
    constexpr int kAudioChannels = 2;
    constexpr int kAudioBitrate = 192000;
    constexpr int kAudioFrameSize = 1024;
    constexpr int kAvFrameCount = 60;
    constexpr int kAudioFramesPerVideo = kAudioSampleRate / kFps;
    constexpr double kPi = 3.14159265358979323846;

    hmmob::D3D11Device d3d;
    if (!d3d.Create()) {
        SKIP("D3D11 device unavailable");
    }

    hmmob::AmfEncoder encoder;
    if (!encoder.Initialize(d3d.device(), "hevc", kWidth, kHeight, kFps, kBitrate)) {
        SKIP("AMF hevc encoder unavailable");
    }

    hmmob::AacEncoder aac;
    REQUIRE(aac.Initialize(kAudioSampleRate, kAudioChannels, kAudioBitrate));
    REQUIRE(aac.initial_padding_samples() == kAudioFrameSize);
    REQUIRE_FALSE(aac.extradata().empty());

    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = kWidth;
    desc.Height = kHeight;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_NV12;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
    REQUIRE(SUCCEEDED(d3d.device()->CreateTexture2D(&desc, nullptr, &texture)));

    TempFile output{UniqueTempPath("hevc-aac")};
    const std::string path = output.path.string();
    const int expected_initial_padding = aac.initial_padding_samples();
    const int expected_extradata_size =
        static_cast<int>(aac.extradata().size());

    hmmob::AudioConfig audio;
    audio.enabled = true;
    audio.sample_rate = aac.sample_rate();
    audio.channels = aac.channels();
    audio.bitrate = aac.bitrate();
    audio.frame_size = aac.frame_size();
    audio.initial_padding = aac.initial_padding_samples();
    audio.extradata = aac.extradata();

    hmmob::FfmpegMuxer muxer;
    REQUIRE(muxer.Open(path, "hevc", kWidth, kHeight, kFps, kBitrate, audio));

    int video_packets_written = 0;
    int audio_packets_written = 0;
    const auto write_video = [&](const hmmob::AmfPacket& packet) {
        REQUIRE_FALSE(packet.data.empty());
        REQUIRE(muxer.WritePacket(packet.data.data(), packet.data.size(),
                                  packet.pts_us, packet.keyframe));
        ++video_packets_written;
    };
    const auto write_audio = [&](const hmmob::AacPacket& packet) {
        REQUIRE_FALSE(packet.data.empty());
        const int64_t pts_us =
            packet.pts_samples * 1'000'000 / kAudioSampleRate;
        REQUIRE(muxer.WriteAudioPacket(packet.data.data(), packet.data.size(),
                                       pts_us, packet.duration_samples));
        ++audio_packets_written;
    };

    std::vector<uint8_t> nv12(kWidth * kHeight * 3 / 2, 128);
    std::vector<float> pcm(
        static_cast<size_t>(kAudioFramesPerVideo) * kAudioChannels);
    int64_t audio_samples_fed = 0;
    for (int i = 0; i < kAvFrameCount; ++i) {
        for (int f = 0; f < kAudioFramesPerVideo; ++f) {
            const double t = static_cast<double>(audio_samples_fed + f) /
                             kAudioSampleRate;
            const float sample =
                static_cast<float>(0.5 * std::sin(2.0 * kPi * 440.0 * t));
            pcm[static_cast<size_t>(f) * kAudioChannels + 0] = sample;
            pcm[static_cast<size_t>(f) * kAudioChannels + 1] = sample;
        }
        REQUIRE(aac.Encode(pcm.data(), kAudioFramesPerVideo, write_audio));
        audio_samples_fed += kAudioFramesPerVideo;

        nv12[0] = static_cast<uint8_t>((i * 37) & 0xFF);
        nv12[1] = static_cast<uint8_t>((i * 73) & 0xFF);
        d3d.context()->UpdateSubresource(texture.Get(), 0, nullptr, nv12.data(),
                                         kWidth, kWidth * kHeight);
        REQUIRE(encoder.Encode(texture.Get(), i * kFrameUs, write_video));
    }
    REQUIRE(aac.Flush(write_audio));
    REQUIRE(encoder.Flush(write_video));
    REQUIRE(muxer.Close());

    AVFormatContext* format = nullptr;
    REQUIRE(avformat_open_input(&format, path.c_str(), nullptr, nullptr) >= 0);
    REQUIRE(format != nullptr);
    REQUIRE(avformat_find_stream_info(format, nullptr) >= 0);

    CHECK(format->nb_streams == 2);
    const int video_index =
        av_find_best_stream(format, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    const int audio_index =
        av_find_best_stream(format, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    REQUIRE(video_index >= 0);
    REQUIRE(audio_index >= 0);

    const AVStream* video_stream = format->streams[video_index];
    const AVStream* audio_stream = format->streams[audio_index];
    CHECK(video_stream->codecpar->codec_id == AV_CODEC_ID_HEVC);
    CHECK(audio_stream->codecpar->codec_id == AV_CODEC_ID_AAC);
    CHECK(audio_stream->codecpar->sample_rate == kAudioSampleRate);
    CHECK(audio_stream->codecpar->ch_layout.nb_channels == kAudioChannels);
    CHECK(audio_stream->codecpar->frame_size == kAudioFrameSize);
    CHECK(audio_stream->codecpar->initial_padding == expected_initial_padding);
    CHECK(audio_stream->codecpar->extradata_size == expected_extradata_size);

    const AVCodec* audio_decoder = avcodec_find_decoder(AV_CODEC_ID_AAC);
    REQUIRE(audio_decoder != nullptr);
    AVCodecContext* audio_decoder_context =
        avcodec_alloc_context3(audio_decoder);
    REQUIRE(audio_decoder_context != nullptr);
    REQUIRE(avcodec_parameters_to_context(audio_decoder_context,
                                          audio_stream->codecpar) >= 0);
    REQUIRE(avcodec_open2(audio_decoder_context, audio_decoder, nullptr) >= 0);

    AVPacket* packet = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();
    REQUIRE(packet != nullptr);
    REQUIRE(frame != nullptr);

    std::vector<int64_t> audio_durations;
    int audio_frames_decoded = 0;
    int video_packets_demuxed = 0;
    int64_t first_audio_pts = AV_NOPTS_VALUE;
    int read_result = 0;
    while ((read_result = av_read_frame(format, packet)) >= 0) {
        if (packet->stream_index == video_index) {
            ++video_packets_demuxed;
        }
        if (packet->stream_index == audio_index) {
            if (first_audio_pts == AV_NOPTS_VALUE) {
                first_audio_pts = packet->pts;
            }
            CHECK(packet->pts == packet->dts);
            audio_durations.push_back(packet->duration);
            REQUIRE(avcodec_send_packet(audio_decoder_context, packet) >= 0);
            int receive = 0;
            while ((receive = avcodec_receive_frame(audio_decoder_context, frame)) >= 0) {
                ++audio_frames_decoded;
                CHECK(frame->sample_rate == kAudioSampleRate);
                CHECK(frame->ch_layout.nb_channels == kAudioChannels);
                av_frame_unref(frame);
            }
            REQUIRE((receive == AVERROR(EAGAIN) || receive == AVERROR_EOF));
        }
        av_packet_unref(packet);
    }
    REQUIRE(read_result == AVERROR_EOF);

    REQUIRE(avcodec_send_packet(audio_decoder_context, nullptr) >= 0);
    int drain = 0;
    while ((drain = avcodec_receive_frame(audio_decoder_context, frame)) >= 0) {
        ++audio_frames_decoded;
        av_frame_unref(frame);
    }
    REQUIRE((drain == AVERROR_EOF || drain == AVERROR(EAGAIN)));

    CHECK(video_packets_written >= kAvFrameCount);
    CHECK(audio_packets_written > 0);
    CHECK(audio_frames_decoded >= 1);
    CHECK(first_audio_pts == -kAudioFrameSize);
    CHECK(audio_durations.size() ==
          static_cast<size_t>(audio_packets_written));
    // ~94 packets for 2 s at 1024 samples each; the final one may be shorter.
    REQUIRE(audio_durations.size() >= 55);
    for (size_t i = 0; i + 1 < audio_durations.size(); ++i) {
        INFO("audio packet " << i << " duration=" << audio_durations[i]);
        CHECK(audio_durations[i] == kAudioFrameSize);
    }

    // The container and both tracks must span about two seconds and end
    // within 50 ms of each other.
    REQUIRE(format->duration != AV_NOPTS_VALUE);
    const double format_seconds =
        static_cast<double>(format->duration) / AV_TIME_BASE;
    CHECK(format_seconds > 1.9);
    CHECK(format_seconds < 2.2);
    REQUIRE(video_stream->duration != AV_NOPTS_VALUE);
    REQUIRE(audio_stream->duration != AV_NOPTS_VALUE);
    CHECK(audio_stream->duration ==
          static_cast<int64_t>(kAvFrameCount) * kAudioFramesPerVideo);
    const double video_seconds = static_cast<double>(video_stream->duration) *
                                 av_q2d(video_stream->time_base);
    const double audio_seconds = static_cast<double>(audio_stream->duration) *
                                 av_q2d(audio_stream->time_base);
    INFO("video=" << video_seconds << "s audio=" << audio_seconds << "s");
    INFO("format=" << format_seconds << "s written video=" << video_packets_written
                    << " audio=" << audio_packets_written
                    << " demuxed video=" << video_packets_demuxed
                    << " audio=" << audio_durations.size()
                    << " first audio pts=" << first_audio_pts
                    << " audio start_time=" << audio_stream->start_time
                    << " video start_time=" << video_stream->start_time
                    << " last audio duration="
                    << (audio_durations.empty() ? -1 : audio_durations.back()));
    CHECK(std::abs(video_seconds - audio_seconds) < 0.005);

    av_frame_free(&frame);
    av_packet_free(&packet);
    avcodec_free_context(&audio_decoder_context);
    avformat_close_input(&format);
}

TEST_CASE("FFmpeg muxer buffers audio until the video header is written",
          "[integration][amf]") {
    constexpr int kAudioSampleRate = 48000;
    constexpr int kAudioChannels = 2;
    constexpr int kAudioBitrate = 192000;
    constexpr int kAudioFrameSize = 1024;
    constexpr int kVideoFrameCount = 30;
    constexpr int kAudioFramesPerVideo = kAudioSampleRate / kFps;
    constexpr double kPi = 3.14159265358979323846;

    hmmob::D3D11Device d3d;
    if (!d3d.Create()) {
        SKIP("D3D11 device unavailable");
    }

    hmmob::AmfEncoder encoder;
    if (!encoder.Initialize(d3d.device(), "hevc", kWidth, kHeight, kFps, kBitrate)) {
        SKIP("AMF hevc encoder unavailable");
    }

    hmmob::AacEncoder aac;
    REQUIRE(aac.Initialize(kAudioSampleRate, kAudioChannels, kAudioBitrate));
    REQUIRE(aac.initial_padding_samples() == kAudioFrameSize);
    REQUIRE_FALSE(aac.extradata().empty());

    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = kWidth;
    desc.Height = kHeight;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_NV12;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
    REQUIRE(SUCCEEDED(d3d.device()->CreateTexture2D(&desc, nullptr, &texture)));

    TempFile output{UniqueTempPath("audio-buffered")};
    const std::string path = output.path.string();

    hmmob::AudioConfig audio;
    audio.enabled = true;
    audio.sample_rate = kAudioSampleRate;
    audio.channels = kAudioChannels;
    audio.bitrate = kAudioBitrate;
    audio.frame_size = kAudioFrameSize;
    audio.initial_padding = aac.initial_padding_samples();
    audio.extradata = aac.extradata();

    hmmob::FfmpegMuxer muxer;
    REQUIRE(muxer.Open(path, "hevc", kWidth, kHeight, kFps, kBitrate, audio));

    bool video_started = false;
    int audio_packets_written = 0;
    std::vector<int64_t> pre_header_pts;
    std::vector<int> pre_header_sizes;
    const auto write_audio = [&](const hmmob::AacPacket& packet) {
        REQUIRE_FALSE(packet.data.empty());
        const int64_t pts_us =
            packet.pts_samples * 1'000'000 / kAudioSampleRate;
        REQUIRE(muxer.WriteAudioPacket(packet.data.data(), packet.data.size(),
                                       pts_us, packet.duration_samples));
        if (!video_started) {
            pre_header_pts.push_back(packet.pts_samples);
            pre_header_sizes.push_back(static_cast<int>(packet.data.size()));
        }
        ++audio_packets_written;
    };

    // Feed AAC frames before any video packet. WriteAudioPacket must accept
    // and buffer them because the header does not exist yet. The encoder
    // delays its first packet, so feed until one actually arrives.
    std::vector<float> pcm(
        static_cast<size_t>(kAudioFramesPerVideo) * kAudioChannels);
    int64_t audio_samples_fed = 0;
    while (pre_header_pts.empty()) {
        for (int f = 0; f < kAudioFrameSize; ++f) {
            const double t =
                static_cast<double>(audio_samples_fed + f) / kAudioSampleRate;
            const float sample =
                static_cast<float>(0.5 * std::sin(2.0 * kPi * 440.0 * t));
            pcm[static_cast<size_t>(f) * kAudioChannels + 0] = sample;
            pcm[static_cast<size_t>(f) * kAudioChannels + 1] = sample;
        }
        REQUIRE(aac.Encode(pcm.data(), kAudioFrameSize, write_audio));
        audio_samples_fed += kAudioFrameSize;
        REQUIRE(audio_samples_fed <= kAudioSampleRate);
    }
    REQUIRE(pre_header_pts.size() >= 1);
    CHECK(pre_header_pts.front() == -kAudioFrameSize);
    for (size_t i = 1; i < pre_header_pts.size(); ++i) {
        CHECK(pre_header_pts[i] == pre_header_pts[i - 1] + kAudioFrameSize);
    }

    int video_packets_written = 0;
    const auto write_video = [&](const hmmob::AmfPacket& packet) {
        REQUIRE_FALSE(packet.data.empty());
        REQUIRE(muxer.WritePacket(packet.data.data(), packet.data.size(),
                                  packet.pts_us, packet.keyframe));
        ++video_packets_written;
    };

    std::vector<uint8_t> nv12(kWidth * kHeight * 3 / 2, 128);
    nv12[0] = 0;
    nv12[1] = 0;
    d3d.context()->UpdateSubresource(texture.Get(), 0, nullptr, nv12.data(),
                                     kWidth, kWidth * kHeight);
    REQUIRE(encoder.Encode(texture.Get(), 0, write_video));
    video_started = true;

    for (int i = 1; i < kVideoFrameCount; ++i) {
        for (int f = 0; f < kAudioFramesPerVideo; ++f) {
            const double t =
                static_cast<double>(audio_samples_fed + f) / kAudioSampleRate;
            const float sample =
                static_cast<float>(0.5 * std::sin(2.0 * kPi * 440.0 * t));
            pcm[static_cast<size_t>(f) * kAudioChannels + 0] = sample;
            pcm[static_cast<size_t>(f) * kAudioChannels + 1] = sample;
        }
        REQUIRE(aac.Encode(pcm.data(), kAudioFramesPerVideo, write_audio));
        audio_samples_fed += kAudioFramesPerVideo;

        nv12[0] = static_cast<uint8_t>((i * 37) & 0xFF);
        nv12[1] = static_cast<uint8_t>((i * 73) & 0xFF);
        d3d.context()->UpdateSubresource(texture.Get(), 0, nullptr, nv12.data(),
                                         kWidth, kWidth * kHeight);
        REQUIRE(encoder.Encode(texture.Get(), i * kFrameUs, write_video));
    }
    REQUIRE(aac.Flush(write_audio));
    REQUIRE(encoder.Flush(write_video));
    REQUIRE(muxer.Close());

    AVFormatContext* format = nullptr;
    REQUIRE(avformat_open_input(&format, path.c_str(), nullptr, nullptr) >= 0);
    REQUIRE(format != nullptr);
    REQUIRE(avformat_find_stream_info(format, nullptr) >= 0);

    const int video_index =
        av_find_best_stream(format, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    const int audio_index =
        av_find_best_stream(format, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    REQUIRE(video_index >= 0);
    REQUIRE(audio_index >= 0);

    AVPacket* packet = av_packet_alloc();
    REQUIRE(packet != nullptr);
    std::vector<int64_t> audio_pts;
    std::vector<int> audio_sizes;
    std::vector<int64_t> audio_durations;
    int video_packets_demuxed = 0;
    int read_result = 0;
    while ((read_result = av_read_frame(format, packet)) >= 0) {
        if (packet->stream_index == video_index) {
            ++video_packets_demuxed;
        } else if (packet->stream_index == audio_index) {
            audio_pts.push_back(packet->pts);
            audio_sizes.push_back(packet->size);
            audio_durations.push_back(packet->duration);
        }
        av_packet_unref(packet);
    }
    REQUIRE(read_result == AVERROR_EOF);
    av_packet_free(&packet);
    avformat_close_input(&format);

    // The two pre-header packets must appear first, unchanged and in order.
    REQUIRE(audio_pts.size() >= pre_header_pts.size());
    for (size_t i = 0; i < pre_header_pts.size(); ++i) {
        INFO("pre-header packet " << i << " pts=" << audio_pts[i]);
        CHECK(audio_pts[i] == pre_header_pts[i]);
        CHECK(audio_sizes[i] == pre_header_sizes[i]);
    }
    CHECK(video_packets_written >= kVideoFrameCount);
    CHECK(video_packets_demuxed >= kVideoFrameCount);
    CHECK(audio_pts.size() > pre_header_pts.size());

    // The first packet keeps its negative priming PTS; the timeline is
    // contiguous (each PTS advances by the previous packet's duration).
    REQUIRE_FALSE(audio_pts.empty());
    {
        INFO("written video=" << video_packets_written
                              << " demuxed video=" << video_packets_demuxed
                              << " demuxed audio=" << audio_pts.size()
                              << " pre-header audio=" << pre_header_pts.size()
                              << " first audio pts=" << audio_pts.front());
        CHECK(audio_pts.front() == -kAudioFrameSize);
    }
    int64_t expected_pts = audio_pts.front();
    for (size_t i = 0; i < audio_pts.size(); ++i) {
        INFO("audio packet " << i << " pts=" << audio_pts[i]
                             << " expected=" << expected_pts);
        CHECK(audio_pts[i] == expected_pts);
        expected_pts += audio_durations[i];
    }
}

TEST_CASE("FfmpegMuxer rejects enabled audio without extradata", "[mux]") {
    TempFile output{UniqueTempPath("audio-no-extradata")};
    const std::string path = output.path.string();

    hmmob::AudioConfig audio;
    audio.enabled = true;

    hmmob::FfmpegMuxer muxer;
    // AAC in MP4 requires an AudioSpecificConfig.
    CHECK_FALSE(muxer.Open(path, "h264", kWidth, kHeight, kFps, kBitrate, audio));
    CHECK_FALSE(std::filesystem::exists(output.path));

    // Every audio field must be positive.
    audio.extradata = {0x11, 0x90};
    audio.sample_rate = 0;
    CHECK_FALSE(muxer.Open(path, "h264", kWidth, kHeight, kFps, kBitrate, audio));
    audio.sample_rate = 48000;
    audio.channels = 0;
    CHECK_FALSE(muxer.Open(path, "h264", kWidth, kHeight, kFps, kBitrate, audio));
    audio.channels = 2;
    audio.bitrate = 0;
    CHECK_FALSE(muxer.Open(path, "h264", kWidth, kHeight, kFps, kBitrate, audio));
    audio.bitrate = 192000;
    audio.frame_size = 0;
    CHECK_FALSE(muxer.Open(path, "h264", kWidth, kHeight, kFps, kBitrate, audio));

    CHECK_FALSE(std::filesystem::exists(output.path));
    CHECK(muxer.Close());
}

TEST_CASE("FfmpegMuxer rejects invalid Open arguments", "[mux]") {
    TempFile output{UniqueTempPath("invalid")};
    const std::string path = output.path.string();
    hmmob::FfmpegMuxer muxer;

    CHECK_FALSE(muxer.Open("", "h264", kWidth, kHeight, kFps, kBitrate));
    CHECK_FALSE(muxer.Open(path, "vp9", kWidth, kHeight, kFps, kBitrate));
    CHECK_FALSE(muxer.Open(path, "h264", 0, kHeight, kFps, kBitrate));
    CHECK_FALSE(muxer.Open(path, "h264", kWidth, 0, kFps, kBitrate));
    CHECK_FALSE(muxer.Open(path, "h264", kWidth, kHeight, 0, kBitrate));
    CHECK_FALSE(muxer.Open(path, "h264", kWidth, kHeight, kFps, 0));

    // Rejected arguments must not create the output file.
    CHECK_FALSE(std::filesystem::exists(output.path));
    CHECK(muxer.Close());  // Never opened: nothing to report.
}

TEST_CASE("FfmpegMuxer Close before any packet reports failure", "[mux]") {
    TempFile output{UniqueTempPath("empty")};
    hmmob::FfmpegMuxer muxer;
    REQUIRE(muxer.Open(output.path.string(), "h264", kWidth, kHeight, kFps, kBitrate));

    CHECK_FALSE(muxer.Close());
    // The documented behaviour is to leave the empty file behind.
    CHECK(std::filesystem::exists(output.path));
    CHECK(std::filesystem::file_size(output.path) == 0);
    CHECK(muxer.Close());  // Idempotent.
}

TEST_CASE("FfmpegMuxer latches write failures", "[mux]") {
    TempFile output{UniqueTempPath("latch")};
    hmmob::FfmpegMuxer muxer;
    REQUIRE(muxer.Open(output.path.string(), "h264", kWidth, kHeight, kFps, kBitrate));

    const Bytes no_nal = {0xDE, 0xAD, 0xBE, 0xEF};
    CHECK_FALSE(muxer.WritePacket(no_nal.data(), no_nal.size(), 0, true));

    // Even a valid packet is rejected once the session has failed.
    const Bytes keyframe = SyntheticH264Keyframe();
    CHECK_FALSE(muxer.WritePacket(keyframe.data(), keyframe.size(), 0, true));
    CHECK_FALSE(muxer.Close());
}

TEST_CASE("FfmpegMuxer writes synthetic access units", "[mux]") {
    TempFile output{UniqueTempPath("synthetic")};
    hmmob::FfmpegMuxer muxer;
    REQUIRE(muxer.Open(output.path.string(), "h264", kWidth, kHeight, kFps, kBitrate));

    const Bytes keyframe = SyntheticH264Keyframe();
    REQUIRE(muxer.WritePacket(keyframe.data(), keyframe.size(), 0, true));
    const Bytes inter = SyntheticH264InterFrame();
    REQUIRE(muxer.WritePacket(inter.data(), inter.size(), 33333, false));
    REQUIRE(muxer.Close());
    CHECK(muxer.Close());  // Idempotent after a successful session.
    CHECK(std::filesystem::file_size(output.path) > 0);
}

TEST_CASE("FfmpegMuxer buffers audio packets before the video header", "[mux]") {
    TempFile output{UniqueTempPath("audio-preheader")};
    const std::string path = output.path.string();
    hmmob::FfmpegMuxer muxer;
    REQUIRE(muxer.Open(path, "h264", kWidth, kHeight, kFps, kBitrate,
                       TestAudioConfig()));

    const Bytes first = DummyAacPacket(0x11);
    const Bytes second = DummyAacPacket(0x22, 96);
    REQUIRE(muxer.WriteAudioPacket(first.data(), first.size(),
                                   AudioSamplesToUs(-kTestAudioFrameSize),
                                   kTestAudioFrameSize));
    REQUIRE(muxer.WriteAudioPacket(second.data(), second.size(),
                                   AudioSamplesToUs(0), kTestAudioFrameSize));

    // The first video packet writes the header and flushes the buffer.
    const Bytes keyframe = SyntheticH264Keyframe();
    REQUIRE(muxer.WritePacket(keyframe.data(), keyframe.size(), 0, true));
    REQUIRE(muxer.Close());

    AVFormatContext* format = nullptr;
    REQUIRE(avformat_open_input(&format, path.c_str(), nullptr, nullptr) >= 0);
    REQUIRE(format != nullptr);
    REQUIRE(avformat_find_stream_info(format, nullptr) >= 0);

    const int audio_index =
        av_find_best_stream(format, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    REQUIRE(audio_index >= 0);
    const AVStream* audio_stream = format->streams[audio_index];
    CHECK(audio_stream->codecpar->sample_rate == kTestAudioSampleRate);
    CHECK(audio_stream->codecpar->ch_layout.nb_channels == 2);
    CHECK(audio_stream->time_base.num == 1);
    CHECK(audio_stream->time_base.den == kTestAudioSampleRate);

    AVPacket* packet = av_packet_alloc();
    REQUIRE(packet != nullptr);
    std::vector<int64_t> pts;
    std::vector<Bytes> payloads;
    int read_result = 0;
    while ((read_result = av_read_frame(format, packet)) >= 0) {
        if (packet->stream_index == audio_index) {
            pts.push_back(packet->pts);
            payloads.emplace_back(packet->data, packet->data + packet->size);
        }
        av_packet_unref(packet);
    }
    REQUIRE(read_result == AVERROR_EOF);
    av_packet_free(&packet);
    avformat_close_input(&format);

    REQUIRE(payloads.size() == 2);
    CHECK(pts[0] == -kTestAudioFrameSize);
    CHECK(pts[1] == 0);
    CHECK(payloads[0] == first);
    CHECK(payloads[1] == second);
}

TEST_CASE("FfmpegMuxer caps audio buffered before the video header", "[mux]") {
    TempFile output{UniqueTempPath("audio-overflow")};
    hmmob::FfmpegMuxer muxer;
    REQUIRE(muxer.Open(output.path.string(), "h264", kWidth, kHeight, kFps,
                       kBitrate, TestAudioConfig()));

    const Bytes payload = DummyAacPacket(0x33);
    REQUIRE(muxer.WriteAudioPacket(payload.data(), payload.size(), 0,
                                   kTestAudioFrameSize));
    // More than 5 s after the first buffered packet overflows the buffer.
    CHECK_FALSE(muxer.WriteAudioPacket(payload.data(), payload.size(),
                                       5'000'001, kTestAudioFrameSize));
    // The failure is sticky: audio and video writes both fail afterwards.
    CHECK_FALSE(muxer.WriteAudioPacket(payload.data(), payload.size(), 0,
                                       kTestAudioFrameSize));
    const Bytes keyframe = SyntheticH264Keyframe();
    CHECK_FALSE(muxer.WritePacket(keyframe.data(), keyframe.size(), 0, true));
    CHECK_FALSE(muxer.Close());
}

TEST_CASE("FfmpegMuxer caps pre-header audio by sample count", "[mux]") {
    TempFile output{UniqueTempPath("audio-sample-cap")};
    hmmob::FfmpegMuxer muxer;
    REQUIRE(muxer.Open(output.path.string(), "h264", kWidth, kHeight, kFps,
                       kBitrate, TestAudioConfig()));

    const Bytes payload = DummyAacPacket(0x44);
    // Non-monotonic (decreasing) PTS defeat the timestamp check, so the
    // five-second sample cap must stop the buffer: 5 * 48000 / 1024 = 234
    // packets fit, the 235th is rejected.
    int accepted = 0;
    int rejected_at = -1;
    for (int i = 0; i < 400; ++i) {
        const int64_t pts_us = -static_cast<int64_t>(i) * 21333;
        if (!muxer.WriteAudioPacket(payload.data(), payload.size(), pts_us,
                                    kTestAudioFrameSize)) {
            rejected_at = i;
            break;
        }
        ++accepted;
    }
    CHECK(rejected_at == 234);
    CHECK(accepted == 234);
    CHECK_FALSE(muxer.Close());
}

TEST_CASE("FfmpegMuxer Close fails after only audio was buffered", "[mux]") {
    TempFile output{UniqueTempPath("audio-only")};
    hmmob::FfmpegMuxer muxer;
    REQUIRE(muxer.Open(output.path.string(), "h264", kWidth, kHeight, kFps,
                       kBitrate, TestAudioConfig()));

    const Bytes payload = DummyAacPacket(0x55);
    REQUIRE(muxer.WriteAudioPacket(payload.data(), payload.size(), 0,
                                   kTestAudioFrameSize));
    CHECK_FALSE(muxer.Close());
    CHECK(std::filesystem::exists(output.path));
    CHECK(std::filesystem::file_size(output.path) == 0);
    CHECK(muxer.Close());  // Idempotent.
}

TEST_CASE("FfmpegMuxer rejects audio packets when audio is disabled", "[mux]") {
    TempFile output{UniqueTempPath("audio-disabled")};
    hmmob::FfmpegMuxer muxer;
    REQUIRE(muxer.Open(output.path.string(), "h264", kWidth, kHeight, kFps,
                       kBitrate));

    const Bytes payload = DummyAacPacket(0x66);
    CHECK_FALSE(muxer.WriteAudioPacket(payload.data(), payload.size(), 0,
                                       kTestAudioFrameSize));

    // A missing audio stream is a caller error, not a write failure: the
    // video session continues normally.
    const Bytes keyframe = SyntheticH264Keyframe();
    REQUIRE(muxer.WritePacket(keyframe.data(), keyframe.size(), 0, true));
    REQUIRE(muxer.Close());
}
