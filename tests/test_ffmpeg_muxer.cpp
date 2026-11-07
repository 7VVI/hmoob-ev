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

    TempFile output{std::filesystem::temp_directory_path() /
                    ("hmmob-muxer-test-" + codec + ".mp4")};
    {
        std::error_code error;
        std::filesystem::remove(output.path, error);
    }
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
