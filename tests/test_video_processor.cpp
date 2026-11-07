#include <catch2/catch_test_macros.hpp>
#include <vector>
#include "core/gfx/VideoProcessor.h"

namespace {

constexpr int kWidth = 640;
constexpr int kHeight = 360;

void SetPixel(std::vector<uint8_t>& bgra, int width, int x, int y,
              uint8_t b, uint8_t g, uint8_t r, uint8_t a) {
    const size_t i = (static_cast<size_t>(y) * width + x) * 4;
    bgra[i + 0] = b;
    bgra[i + 1] = g;
    bgra[i + 2] = r;
    bgra[i + 3] = a;
}

Microsoft::WRL::ComPtr<ID3D11Texture2D> CreateBgraTexture(
    ID3D11Device* device, int width, int height, const std::vector<uint8_t>& bgra) {
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = static_cast<UINT>(width);
    desc.Height = static_cast<UINT>(height);
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;

    D3D11_SUBRESOURCE_DATA init{};
    init.pSysMem = bgra.data();
    init.SysMemPitch = static_cast<UINT>(width) * 4;

    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
    if (FAILED(device->CreateTexture2D(&desc, &init, &texture))) {
        return nullptr;
    }
    return texture;
}

}  // namespace

TEST_CASE("VideoProcessor converts BGRA to NV12", "[integration]") {
    hmmob::D3D11Device d3d;
    if (!d3d.Create()) {
        SKIP("D3D11 device unavailable");
    }

    std::vector<uint8_t> bgra(static_cast<size_t>(kWidth) * kHeight * 4, 128);
    for (int y = 0; y < kHeight; ++y) {
        for (int x = 0; x < kWidth; ++x) {
            if (x < kWidth / 2 && y < kHeight / 2) {
                SetPixel(bgra, kWidth, x, y, 128, 128, 128, 255);  // gray
            } else if (x >= kWidth / 2 && y < kHeight / 2) {
                SetPixel(bgra, kWidth, x, y, 255, 255, 255, 255);  // white
            } else if (x < kWidth / 2 && y >= kHeight / 2) {
                SetPixel(bgra, kWidth, x, y, 0, 0, 0, 255);        // black
            } else {
                SetPixel(bgra, kWidth, x, y, 0, 0, 255, 255);      // red
            }
        }
    }

    auto src = CreateBgraTexture(d3d.device(), kWidth, kHeight, bgra);
    REQUIRE(src.Get() != nullptr);

    hmmob::VideoProcessor processor;
    REQUIRE(processor.width() == 0);
    REQUIRE(processor.height() == 0);
    REQUIRE(processor.Initialize(d3d, kWidth, kHeight));
    REQUIRE(processor.width() == kWidth);
    REQUIRE(processor.height() == kHeight);
    REQUIRE(processor.Convert(src.Get(), processor.output_texture()));

    D3D11_TEXTURE2D_DESC out_desc{};
    processor.output_texture()->GetDesc(&out_desc);
    REQUIRE(out_desc.Format == DXGI_FORMAT_NV12);
    REQUIRE(out_desc.Width == kWidth);
    REQUIRE(out_desc.Height == kHeight);
    REQUIRE((out_desc.BindFlags & D3D11_BIND_RENDER_TARGET) != 0);

    D3D11_TEXTURE2D_DESC staging_desc = out_desc;
    staging_desc.Usage = D3D11_USAGE_STAGING;
    staging_desc.BindFlags = 0;
    staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> staging;
    REQUIRE(SUCCEEDED(d3d.device()->CreateTexture2D(&staging_desc, nullptr, &staging)));
    d3d.context()->CopyResource(staging.Get(), processor.output_texture());

    D3D11_MAPPED_SUBRESOURCE mapped{};
    REQUIRE(SUCCEEDED(d3d.context()->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)));

    const auto* base = static_cast<const uint8_t*>(mapped.pData);
    const auto luma_at = [&](int x, int y) {
        return static_cast<int>(base[static_cast<size_t>(y) * mapped.RowPitch + x]);
    };

    const int gray_y = luma_at(kWidth / 4, kHeight / 4);
    const int white_y = luma_at(3 * kWidth / 4, kHeight / 4);
    const int black_y = luma_at(kWidth / 4, 3 * kHeight / 4);
    const int red_y = luma_at(3 * kWidth / 4, 3 * kHeight / 4);

    // NV12 UV plane follows the Y plane: RowPitch * height.
    const uint8_t* uv = base + static_cast<size_t>(mapped.RowPitch) * kHeight;
    const size_t uv_index =
        static_cast<size_t>(kHeight / 8) * mapped.RowPitch + kWidth / 4;
    const int u = static_cast<int>(uv[uv_index]);
    const int v = static_cast<int>(uv[uv_index + 1]);

    d3d.context()->Unmap(staging.Get(), 0);

    // Studio-range BT.709: gray 126, white 235, black 16, red ~63.
    REQUIRE(gray_y >= 115);
    REQUIRE(gray_y <= 135);
    REQUIRE(white_y >= 225);
    REQUIRE(white_y <= 245);
    REQUIRE(black_y >= 8);
    REQUIRE(black_y <= 24);
    REQUIRE(red_y >= 50);
    REQUIRE(red_y <= 75);
    // Neutral gray remains neutral chroma in NV12.
    REQUIRE(u >= 110);
    REQUIRE(u <= 145);
    REQUIRE(v >= 110);
    REQUIRE(v <= 145);
}

TEST_CASE("VideoProcessor rejects Convert before Initialize") {
    hmmob::VideoProcessor processor;
    REQUIRE_FALSE(processor.Convert(nullptr, nullptr));
}

TEST_CASE("VideoProcessor rejects mismatched input dimensions", "[integration]") {
    hmmob::D3D11Device d3d;
    if (!d3d.Create()) {
        SKIP("D3D11 device unavailable");
    }

    hmmob::VideoProcessor processor;
    REQUIRE(processor.Initialize(d3d, kWidth, kHeight));

    constexpr int kSmallWidth = 320;
    constexpr int kSmallHeight = 180;
    std::vector<uint8_t> pixels(
        static_cast<size_t>(kSmallWidth) * kSmallHeight * 4, 128);
    auto src = CreateBgraTexture(d3d.device(), kSmallWidth, kSmallHeight, pixels);
    REQUIRE(src.Get() != nullptr);

    REQUIRE_FALSE(processor.Convert(src.Get(), processor.output_texture()));
}

TEST_CASE("VideoProcessor supports re-initialization", "[integration]") {
    hmmob::D3D11Device d3d;
    if (!d3d.Create()) {
        SKIP("D3D11 device unavailable");
    }

    hmmob::VideoProcessor processor;
    REQUIRE(processor.Initialize(d3d, kWidth, kHeight));
    REQUIRE(processor.width() == kWidth);
    REQUIRE(processor.height() == kHeight);

    REQUIRE(processor.Initialize(d3d, kWidth, kHeight));
    REQUIRE(processor.width() == kWidth);
    REQUIRE(processor.height() == kHeight);

    std::vector<uint8_t> pixels(static_cast<size_t>(kWidth) * kHeight * 4, 128);
    auto src = CreateBgraTexture(d3d.device(), kWidth, kHeight, pixels);
    REQUIRE(src.Get() != nullptr);

    REQUIRE(processor.Convert(src.Get(), processor.output_texture()));
}
