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
    CHECK(audio_stream->codecpar->initial_padding == kAudioFrameSize);
    CHECK(audio_stream->codecpar->extradata_size == 5);

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
    CHECK(std::abs(video_seconds - audio_seconds) < 0.05);

    av_frame_free(&frame);
    av_packet_free(&packet);
    avcodec_free_context(&audio_decoder_context);
    avformat_close_input(&format);
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
