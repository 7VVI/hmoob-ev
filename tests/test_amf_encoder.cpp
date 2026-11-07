#include <catch2/catch_test_macros.hpp>
#include <d3d11.h>
#include <algorithm>
#include <cstdint>
#include <vector>
#include "core/encoder/AmfEncoder.h"
#include "core/gfx/D3D11Device.h"

namespace {

constexpr int kWidth = 1920;
constexpr int kHeight = 1080;
constexpr int kFps = 60;
constexpr int kBitrate = 10'000'000;
constexpr int kFrameCount = 30;
constexpr int64_t kFrameUs = 16667;

std::vector<int> AnnexBNalTypes(const std::vector<uint8_t>& data, bool hevc) {
    std::vector<int> types;
    const size_t size = data.size();
    for (size_t i = 0; i + 2 < size; ++i) {
        size_t pos = 0;
        if (data[i] == 0 && data[i + 1] == 0 && data[i + 2] == 1) {
            pos = i + 3;
        } else if (i + 3 < size && data[i] == 0 && data[i + 1] == 0 &&
                   data[i + 2] == 0 && data[i + 3] == 1) {
            pos = i + 4;
        } else {
            continue;
        }
        if (pos >= size) {
            continue;
        }
        types.push_back(hevc ? ((data[pos] >> 1) & 0x3F) : (data[pos] & 0x1F));
        i = pos - 1;
    }
    return types;
}

struct EncodeRun {
    std::vector<hmmob::AmfPacket> packets;
    size_t bytes = 0;
};

EncodeRun RunEncoder(const char* codec) {
    hmmob::D3D11Device d3d;
    if (!d3d.Create()) {
        SKIP("D3D11 device unavailable");
    }

    hmmob::AmfEncoder encoder;
    if (!encoder.Initialize(d3d.device(), codec, kWidth, kHeight, kFps, kBitrate)) {
        SKIP("AMF encoder unavailable");
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

    std::vector<uint8_t> nv12(kWidth * kHeight * 3 / 2, 128);
    EncodeRun run;
    auto collect = [&run](const hmmob::AmfPacket& packet) {
        run.bytes += packet.data.size();
        run.packets.push_back(packet);
    };

    for (int i = 0; i < kFrameCount; ++i) {
        // Vary the luma so every frame differs; reusing the source texture
        // without an AMF-owned copy would corrupt queued frames.
        nv12[0] = static_cast<uint8_t>((i * 37) & 0xFF);
        nv12[1] = static_cast<uint8_t>((i * 73) & 0xFF);
        d3d.context()->UpdateSubresource(texture.Get(), 0, nullptr, nv12.data(),
                                         kWidth, kWidth * kHeight);
        REQUIRE(encoder.Encode(texture.Get(), i * kFrameUs, collect));
    }
    REQUIRE(encoder.Flush(collect));

    return run;
}

void CheckRun(const EncodeRun& run, bool hevc) {
    REQUIRE(run.packets.size() >= static_cast<size_t>(kFrameCount));
    REQUIRE(run.bytes > 0);

    REQUIRE(run.packets.front().pts_us == 0);
    for (size_t i = 1; i < run.packets.size(); ++i) {
        REQUIRE(run.packets[i].pts_us >= run.packets[i - 1].pts_us);
    }

    REQUIRE(run.packets.front().keyframe);
    const std::vector<int> nals = AnnexBNalTypes(run.packets.front().data, hevc);
    auto has_nal = [&nals](int type) {
        return std::find(nals.begin(), nals.end(), type) != nals.end();
    };
    if (hevc) {
        REQUIRE(has_nal(32));  // VPS
        REQUIRE(has_nal(33));  // SPS
        REQUIRE(has_nal(34));  // PPS
    } else {
        REQUIRE(has_nal(7));  // SPS
        REQUIRE(has_nal(8));  // PPS
    }
}

}  // namespace

TEST_CASE("AMF encoder produces HEVC packets", "[integration][amf]") {
    CheckRun(RunEncoder("hevc"), true);
}

TEST_CASE("AMF encoder produces H.264 packets", "[integration][amf]") {
    CheckRun(RunEncoder("h264"), false);
}
