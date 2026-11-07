#include <catch2/catch_test_macros.hpp>
#include <d3d11.h>
#include <wrl/client.h>
#include <cstdint>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/pixfmt.h>
}

#include "core/encoder/AmfEncoder.h"
#include "core/gfx/D3D11Device.h"
#include "core/mux/FfmpegMuxer.h"

namespace {

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

}  // namespace

TEST_CASE("FFmpeg muxer writes a decodable HEVC MP4", "[integration][amf]") {
    hmmob::D3D11Device d3d;
    if (!d3d.Create()) {
        SKIP("D3D11 device unavailable");
    }

    hmmob::AmfEncoder encoder;
    if (!encoder.Initialize(d3d.device(), "hevc", kWidth, kHeight, kFps, kBitrate)) {
        SKIP("AMF HEVC encoder unavailable");
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

    TempFile output{std::filesystem::temp_directory_path() / "hmmob-muxer-test.mp4"};
    {
        std::error_code error;
        std::filesystem::remove(output.path, error);
    }
    const std::string path = output.path.string();

    hmmob::FfmpegMuxer muxer;
    REQUIRE(muxer.Open(path, "hevc", kWidth, kHeight, kFps, kBitrate));

    std::vector<uint8_t> nv12(kWidth * kHeight * 3 / 2, 128);
    int packets_written = 0;
    int keyframes = 0;
    auto write_packet = [&](const hmmob::AmfPacket& packet) {
        REQUIRE(!packet.data.empty());
        REQUIRE(muxer.WritePacket(packet.data.data(), packet.data.size(),
                                  packet.pts_us, packet.keyframe));
        ++packets_written;
        if (packet.keyframe) {
            ++keyframes;
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

    CHECK(keyframes >= 1);
    CHECK(packets_written >= kFrameCount);
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
    CHECK(stream->codecpar->codec_id == AV_CODEC_ID_HEVC);
    CHECK(stream->codecpar->width == kWidth);
    CHECK(stream->codecpar->height == kHeight);
    CHECK(stream->codecpar->extradata_size > 0);

    const AVCodec* decoder = avcodec_find_decoder(AV_CODEC_ID_HEVC);
    REQUIRE(decoder != nullptr);
    AVCodecContext* codec = avcodec_alloc_context3(decoder);
    REQUIRE(codec != nullptr);
    REQUIRE(avcodec_parameters_to_context(codec, stream->codecpar) >= 0);
    REQUIRE(avcodec_open2(codec, decoder, nullptr) >= 0);

    AVPacket* packet = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();
    REQUIRE(packet != nullptr);
    REQUIRE(frame != nullptr);

    int packets_demuxed = 0;
    int frames_decoded = 0;
    while (av_read_frame(format, packet) >= 0) {
        if (packet->stream_index != video_index) {
            av_packet_unref(packet);
            continue;
        }
        ++packets_demuxed;
        const int sent = avcodec_send_packet(codec, packet);
        av_packet_unref(packet);
        REQUIRE(sent >= 0);
        while (avcodec_receive_frame(codec, frame) >= 0) {
            ++frames_decoded;
            if (frames_decoded == 1) {
                CHECK(frame->width == kWidth);
                CHECK(frame->height == kHeight);
                CHECK(frame->format == AV_PIX_FMT_YUV420P);
            }
            av_frame_unref(frame);
        }
    }

    CHECK(packets_demuxed >= kFrameCount);
    CHECK(frames_decoded >= 1);

    av_frame_free(&frame);
    av_packet_free(&packet);
    avcodec_free_context(&codec);
    avformat_close_input(&format);
}
