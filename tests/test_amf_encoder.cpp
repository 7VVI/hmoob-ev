#include <catch2/catch_test_macros.hpp>
#include <d3d11.h>
#include <vector>
#include "core/encoder/AmfEncoder.h"
#include "core/gfx/D3D11Device.h"

TEST_CASE("AMF encoder produces HEVC packets", "[integration][amf]") {
    hmmob::D3D11Device d3d;
    if (!d3d.Create()) {
        SKIP("D3D11 device unavailable");
    }

    hmmob::AmfEncoder encoder;
    if (!encoder.Initialize(d3d.device(), "hevc", 1920, 1080, 60, 10'000'000)) {
        SKIP("AMF encoder unavailable");
    }

    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = 1920;
    desc.Height = 1080;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_NV12;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

    std::vector<uint8_t> nv12(1920 * 1080 * 3 / 2, 128);
    D3D11_SUBRESOURCE_DATA init{};
    init.pSysMem = nv12.data();
    init.SysMemPitch = 1920;
    init.SysMemSlicePitch = 1920 * 1080;

    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
    REQUIRE(SUCCEEDED(d3d.device()->CreateTexture2D(&desc, &init, &texture)));

    int packets = 0;
    size_t bytes = 0;
    for (int i = 0; i < 30; ++i) {
        REQUIRE(encoder.Encode(texture.Get(), i * 16667, [&](const hmmob::AmfPacket& packet) {
            ++packets;
            bytes += packet.data.size();
        }));
    }
    REQUIRE(encoder.Flush([&](const hmmob::AmfPacket& packet) {
        ++packets;
        bytes += packet.data.size();
    }));
    REQUIRE(packets > 0);
    REQUIRE(bytes > 0);
}
