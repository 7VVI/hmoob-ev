#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <cstdlib>
#include <vector>
#include "core/gfx/HdrToneMapper.h"

namespace {

constexpr int kSize = 64;

Microsoft::WRL::ComPtr<ID3D11Texture2D> CreateTexture(
    ID3D11Device* device, DXGI_FORMAT format, UINT bind_flags) {
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = kSize;
    desc.Height = kSize;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = format;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = bind_flags;

    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
    if (FAILED(device->CreateTexture2D(&desc, nullptr, &texture))) {
        return nullptr;
    }
    return texture;
}

// Returns the BGRA bytes of the first pixel, or an empty vector on failure.
std::vector<uint8_t> ReadFirstPixel(ID3D11Device* device,
                                    ID3D11DeviceContext* context,
                                    ID3D11Texture2D* texture) {
    D3D11_TEXTURE2D_DESC desc{};
    texture->GetDesc(&desc);
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    desc.MiscFlags = 0;

    Microsoft::WRL::ComPtr<ID3D11Texture2D> staging;
    if (FAILED(device->CreateTexture2D(&desc, nullptr, &staging))) {
        return {};
    }
    context->CopyResource(staging.Get(), texture);

    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) {
        return {};
    }
    const auto* base = static_cast<const uint8_t*>(mapped.pData);
    std::vector<uint8_t> pixel(base, base + 4);
    context->Unmap(staging.Get(), 0);
    return pixel;
}

// A neutral BT.2020 white must remain neutral after conversion to BT.709.
// This is the check that catches a transposed color matrix, which tints
// neutral white (the old bug produced roughly B=171, G=116, R=203).
void CheckNeutralSdrPixel(const std::vector<uint8_t>& pixel) {
    REQUIRE(pixel.size() == 4);
    INFO("pixel B=" << +pixel[0] << " G=" << +pixel[1] << " R=" << +pixel[2]
                    << " A=" << +pixel[3]);
    // B, G, R must be visible, unclipped, and in the mid-range expected after
    // Reinhard rolloff (80 nits scRGB -> ~145, PQ 0.5 -> ~152).
    for (int i = 0; i < 3; ++i) {
        REQUIRE(pixel[i] > 0);
        REQUIRE(pixel[i] < 255);
        REQUIRE(pixel[i] >= 120);
        REQUIRE(pixel[i] <= 180);
    }
    REQUIRE(std::abs(pixel[0] - pixel[1]) <= 2);
    REQUIRE(std::abs(pixel[1] - pixel[2]) <= 2);
    REQUIRE(pixel[3] == 255);
}

}  // namespace

TEST_CASE("HdrToneMapper maps FP16 scRGB to BGRA8", "[integration]") {
    hmmob::D3D11Device d3d;
    if (!d3d.Create()) {
        SKIP("D3D11 device unavailable");
    }

    hmmob::HdrToneMapper mapper;
    REQUIRE(mapper.Initialize(d3d, kSize, kSize, false));
    REQUIRE(mapper.width() == kSize);
    REQUIRE(mapper.height() == kSize);

    auto input = CreateTexture(d3d.device(), DXGI_FORMAT_R16G16B16A16_FLOAT,
                               D3D11_BIND_SHADER_RESOURCE);
    REQUIRE(input.Get() != nullptr);

    // The texture stores IEEE binary16 halfs; 0x3C00 is half 1.0. Uploading
    // float32 data would be silently reinterpreted as half pairs.
    constexpr uint16_t kHalfOne = 0x3C00;
    const std::vector<uint16_t> white(static_cast<size_t>(kSize) * kSize * 4,
                                      kHalfOne);
    d3d.context()->UpdateSubresource(input.Get(), 0, nullptr, white.data(),
                                     kSize * 8, 0);

    ID3D11Texture2D* output = mapper.Map(input.Get());
    REQUIRE(output != nullptr);
    REQUIRE(output == mapper.output_texture());

    D3D11_TEXTURE2D_DESC out_desc{};
    output->GetDesc(&out_desc);
    REQUIRE(out_desc.Format == DXGI_FORMAT_B8G8R8A8_UNORM);
    REQUIRE(out_desc.Width == kSize);
    REQUIRE(out_desc.Height == kSize);

    CheckNeutralSdrPixel(ReadFirstPixel(d3d.device(), d3d.context(), output));
}

TEST_CASE("HdrToneMapper maps PQ R10G10B10A2 to BGRA8", "[integration]") {
    hmmob::D3D11Device d3d;
    if (!d3d.Create()) {
        SKIP("D3D11 device unavailable");
    }

    hmmob::HdrToneMapper mapper;
    REQUIRE(mapper.Initialize(d3d, kSize, kSize, true));

    auto input = CreateTexture(d3d.device(), DXGI_FORMAT_R10G10B10A2_UNORM,
                               D3D11_BIND_SHADER_RESOURCE);
    REQUIRE(input.Get() != nullptr);

    // R=G=B=512 (PQ ~0.5), A=1023.
    const uint32_t packed = 512u | (512u << 10) | (512u << 20) | (3u << 30);
    const std::vector<uint32_t> pq(static_cast<size_t>(kSize) * kSize, packed);
    d3d.context()->UpdateSubresource(input.Get(), 0, nullptr, pq.data(),
                                     kSize * 4, 0);

    ID3D11Texture2D* output = mapper.Map(input.Get());
    REQUIRE(output != nullptr);

    CheckNeutralSdrPixel(ReadFirstPixel(d3d.device(), d3d.context(), output));
}

TEST_CASE("HdrToneMapper rejects unsupported input format", "[integration]") {
    hmmob::D3D11Device d3d;
    if (!d3d.Create()) {
        SKIP("D3D11 device unavailable");
    }

    hmmob::HdrToneMapper mapper;
    REQUIRE(mapper.Initialize(d3d, kSize, kSize, false));

    auto input = CreateTexture(d3d.device(), DXGI_FORMAT_B8G8R8A8_UNORM,
                               D3D11_BIND_SHADER_RESOURCE);
    REQUIRE(input.Get() != nullptr);
    REQUIRE(mapper.Map(input.Get()) == nullptr);
}

TEST_CASE("HdrToneMapper rejects mismatched input size", "[integration]") {
    hmmob::D3D11Device d3d;
    if (!d3d.Create()) {
        SKIP("D3D11 device unavailable");
    }

    hmmob::HdrToneMapper mapper;
    REQUIRE(mapper.Initialize(d3d, kSize, kSize, false));

    constexpr int kOtherSize = 32;
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = kOtherSize;
    desc.Height = kOtherSize;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> input;
    REQUIRE(SUCCEEDED(d3d.device()->CreateTexture2D(&desc, nullptr, &input)));
    REQUIRE(mapper.Map(input.Get()) == nullptr);
}

TEST_CASE("HdrToneMapper supports re-initialization", "[integration]") {
    hmmob::D3D11Device d3d;
    if (!d3d.Create()) {
        SKIP("D3D11 device unavailable");
    }

    hmmob::HdrToneMapper mapper;
    REQUIRE(mapper.Initialize(d3d, 32, 32, false));
    REQUIRE(mapper.Initialize(d3d, kSize, kSize, true));
    REQUIRE(mapper.width() == kSize);
    REQUIRE(mapper.height() == kSize);

    auto input = CreateTexture(d3d.device(), DXGI_FORMAT_R10G10B10A2_UNORM,
                               D3D11_BIND_SHADER_RESOURCE);
    REQUIRE(input.Get() != nullptr);
    ID3D11Texture2D* output = mapper.Map(input.Get());
    REQUIRE(output != nullptr);
    REQUIRE(output == mapper.output_texture());
}

TEST_CASE("HdrToneMapper rejects Map before Initialize", "[integration]") {
    hmmob::D3D11Device d3d;
    if (!d3d.Create()) {
        SKIP("D3D11 device unavailable");
    }

    hmmob::HdrToneMapper mapper;
    REQUIRE(mapper.Map(nullptr) == nullptr);
}
