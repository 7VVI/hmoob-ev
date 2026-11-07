#include <catch2/catch_test_macros.hpp>
#include <vector>
#include "core/gfx/VideoProcessor.h"

TEST_CASE("VideoProcessor converts BGRA to NV12", "[integration]") {
    hmmob::D3D11Device d3d;
    if (!d3d.Create()) {
        SKIP("D3D11 device unavailable");
    }

    constexpr int kWidth = 640;
    constexpr int kHeight = 360;

    D3D11_TEXTURE2D_DESC src_desc{};
    src_desc.Width = kWidth;
    src_desc.Height = kHeight;
    src_desc.MipLevels = 1;
    src_desc.ArraySize = 1;
    src_desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    src_desc.SampleDesc.Count = 1;
    src_desc.Usage = D3D11_USAGE_DEFAULT;
    src_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;

    std::vector<uint8_t> bgra(kWidth * kHeight * 4, 128);
    D3D11_SUBRESOURCE_DATA init{};
    init.pSysMem = bgra.data();
    init.SysMemPitch = kWidth * 4;

    Microsoft::WRL::ComPtr<ID3D11Texture2D> src;
    REQUIRE(SUCCEEDED(d3d.device()->CreateTexture2D(&src_desc, &init, &src)));

    hmmob::VideoProcessor processor;
    REQUIRE(processor.Initialize(d3d, kWidth, kHeight));
    REQUIRE(processor.Convert(src.Get(), processor.output_texture()));

    D3D11_TEXTURE2D_DESC out_desc{};
    processor.output_texture()->GetDesc(&out_desc);
    REQUIRE(out_desc.Format == DXGI_FORMAT_NV12);

    D3D11_TEXTURE2D_DESC staging_desc = out_desc;
    staging_desc.Usage = D3D11_USAGE_STAGING;
    staging_desc.BindFlags = 0;
    staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> staging;
    REQUIRE(SUCCEEDED(d3d.device()->CreateTexture2D(&staging_desc, nullptr, &staging)));
    d3d.context()->CopyResource(staging.Get(), processor.output_texture());

    D3D11_MAPPED_SUBRESOURCE mapped{};
    REQUIRE(SUCCEEDED(d3d.context()->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)));
    const uint8_t y = static_cast<const uint8_t*>(mapped.pData)[0];
    d3d.context()->Unmap(staging.Get(), 0);
    REQUIRE(y > 80);
    REQUIRE(y < 180);
}
