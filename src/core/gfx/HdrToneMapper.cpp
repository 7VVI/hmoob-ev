#include "core/gfx/HdrToneMapper.h"

#include <d3dcompiler.h>
#include <cstdio>
#include <cstring>

namespace hmmob {
namespace {

// Keep in sync with shaders/hdr_to_sdr.hlsl.
const char* kShaderSource = R"(
Texture2D<float4> src : register(t0);
SamplerState samp : register(s0);
cbuffer Params : register(b0) {
    float sdr_white_nits;
    float max_nits;  // reserved for a future highlight-rolloff extension
    int pq_mode;
    int pad0;
};
struct VSOut { float4 pos : SV_POSITION; float2 uv : TEXCOORD0; };
VSOut VSMain(uint id : SV_VertexID) {
    float2 p = float2((id << 1) & 2, id & 2);
    VSOut o;
    o.pos = float4(p * float2(2, -2) + float2(-1, 1), 0, 1);
    o.uv = p;
    return o;
}
float3 PqToLinear(float3 pq) {
    const float m1 = 0.1593017578125;
    const float m2 = 78.84375;
    const float c1 = 0.8359375;
    const float c2 = 18.8515625;
    const float c3 = 18.6875;
    float3 p = pow(max(pq, 0.0), 1.0 / m2);
    float3 num = max(p - c1, 0.0);
    float3 den = c2 - c3 * p;
    return pow(num / den, 1.0 / m1) * 10000.0;
}
float3 LinearToSrgb(float3 c) {
    c = max(c, 0.0);
    float3 low = c * 12.92;
    float3 high = 1.055 * pow(c, 1.0 / 2.4) - 0.055;
    return lerp(high, low, step(c, 0.0031308));
}
float4 PSMain(VSOut input) : SV_TARGET {
    float4 sample = src.Sample(samp, input.uv);
    float3 nits = pq_mode ? PqToLinear(sample.rgb) : sample.rgb * 80.0;
    // BT.2020 -> BT.709. Rows sum to ~1, so neutral input stays neutral; a
    // transposed matrix would tint white.
    const float3x3 m = float3x3(
         1.6605, -0.5876, -0.0728,
        -0.1246,  1.1318, -0.0083,
        -0.0182, -0.1006,  1.1183);
    float3 lin = mul(m, nits);
    // Reinhard rolloff relative to SDR white: 203 nits -> ~0.5, no hard clip.
    lin = lin / (lin + max(sdr_white_nits, 1.0));
    return float4(LinearToSrgb(lin), 1.0);
}
)";

struct Constants {
    float sdr_white_nits;
    float max_nits;
    int pq_mode;
    int pad0;
};
static_assert(sizeof(Constants) == 16, "constant buffer must be 16-byte aligned");

bool IsSupportedFormat(DXGI_FORMAT format) {
    return format == DXGI_FORMAT_R16G16B16A16_FLOAT ||
           format == DXGI_FORMAT_R10G10B10A2_UNORM;
}

void LogCompileFailure(const char* entry_point, ID3DBlob* error) {
    const char* message = error
        ? static_cast<const char*>(error->GetBufferPointer())
        : "(no compiler message)";
    std::fprintf(stderr, "%s compile failed: %s\n", entry_point, message);
}

}  // namespace

void HdrToneMapper::Reset() {
    input_srv_.Reset();
    output_rtv_.Reset();
    output_.Reset();
    sampler_.Reset();
    constants_.Reset();
    pixel_shader_.Reset();
    vertex_shader_.Reset();
    cached_input_ = nullptr;
    device_ = nullptr;
    context_ = nullptr;
    width_ = 0;
    height_ = 0;
    pq_mode_ = true;
    ready_ = false;
}

bool HdrToneMapper::Initialize(D3D11Device& d3d, int width, int height, bool pq_mode) {
    Reset();

    if (!d3d.device() || !d3d.context() || width <= 0 || height <= 0) {
        return false;
    }
    device_ = d3d.device();
    context_ = d3d.context();
    width_ = width;
    height_ = height;
    pq_mode_ = pq_mode;

    Microsoft::WRL::ComPtr<ID3DBlob> vs_blob;
    Microsoft::WRL::ComPtr<ID3DBlob> ps_blob;
    Microsoft::WRL::ComPtr<ID3DBlob> error;

    HRESULT hr = D3DCompile(kShaderSource, std::strlen(kShaderSource), nullptr,
                            nullptr, nullptr, "VSMain", "vs_5_0", 0, 0,
                            &vs_blob, &error);
    if (FAILED(hr)) {
        LogCompileFailure("VSMain", error.Get());
        Reset();
        return false;
    }
    error.Reset();
    hr = D3DCompile(kShaderSource, std::strlen(kShaderSource), nullptr,
                    nullptr, nullptr, "PSMain", "ps_5_0", 0, 0,
                    &ps_blob, &error);
    if (FAILED(hr)) {
        LogCompileFailure("PSMain", error.Get());
        Reset();
        return false;
    }

    hr = device_->CreateVertexShader(vs_blob->GetBufferPointer(),
                                     vs_blob->GetBufferSize(), nullptr,
                                     &vertex_shader_);
    if (FAILED(hr)) {
        std::fprintf(stderr, "%s:%d HRESULT=0x%08lx\n", __FILE__, __LINE__,
                     static_cast<unsigned long>(hr));
        Reset();
        return false;
    }
    hr = device_->CreatePixelShader(ps_blob->GetBufferPointer(),
                                    ps_blob->GetBufferSize(), nullptr,
                                    &pixel_shader_);
    if (FAILED(hr)) {
        std::fprintf(stderr, "%s:%d HRESULT=0x%08lx\n", __FILE__, __LINE__,
                     static_cast<unsigned long>(hr));
        Reset();
        return false;
    }

    D3D11_BUFFER_DESC cb_desc{};
    cb_desc.ByteWidth = sizeof(Constants);
    cb_desc.Usage = D3D11_USAGE_DEFAULT;
    cb_desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    hr = device_->CreateBuffer(&cb_desc, nullptr, &constants_);
    if (FAILED(hr)) {
        std::fprintf(stderr, "%s:%d HRESULT=0x%08lx\n", __FILE__, __LINE__,
                     static_cast<unsigned long>(hr));
        Reset();
        return false;
    }

    D3D11_SAMPLER_DESC sampler_desc{};
    sampler_desc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampler_desc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler_desc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler_desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    hr = device_->CreateSamplerState(&sampler_desc, &sampler_);
    if (FAILED(hr)) {
        std::fprintf(stderr, "%s:%d HRESULT=0x%08lx\n", __FILE__, __LINE__,
                     static_cast<unsigned long>(hr));
        Reset();
        return false;
    }

    D3D11_TEXTURE2D_DESC out_desc{};
    out_desc.Width = static_cast<UINT>(width);
    out_desc.Height = static_cast<UINT>(height);
    out_desc.MipLevels = 1;
    out_desc.ArraySize = 1;
    out_desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    out_desc.SampleDesc.Count = 1;
    out_desc.Usage = D3D11_USAGE_DEFAULT;
    out_desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    hr = device_->CreateTexture2D(&out_desc, nullptr, &output_);
    if (FAILED(hr)) {
        std::fprintf(stderr, "%s:%d HRESULT=0x%08lx\n", __FILE__, __LINE__,
                     static_cast<unsigned long>(hr));
        Reset();
        return false;
    }
    hr = device_->CreateRenderTargetView(output_.Get(), nullptr, &output_rtv_);
    if (FAILED(hr)) {
        std::fprintf(stderr, "%s:%d HRESULT=0x%08lx\n", __FILE__, __LINE__,
                     static_cast<unsigned long>(hr));
        Reset();
        return false;
    }

    ready_ = true;
    return true;
}

ID3D11Texture2D* HdrToneMapper::Map(ID3D11Texture2D* hdr_texture) {
    if (!ready_ || !device_ || !context_ || !output_ || !output_rtv_ ||
        !vertex_shader_ || !pixel_shader_ || !constants_ || !sampler_ ||
        !hdr_texture) {
        return nullptr;
    }

    D3D11_TEXTURE2D_DESC desc{};
    hdr_texture->GetDesc(&desc);
    if (!IsSupportedFormat(desc.Format)) {
        std::fprintf(stderr,
                     "HDR tone mapping: unsupported input format %d; expected "
                     "R16G16B16A16_FLOAT or R10G10B10A2_UNORM\n",
                     static_cast<int>(desc.Format));
        return nullptr;
    }
    if (desc.Width != static_cast<UINT>(width_) ||
        desc.Height != static_cast<UINT>(height_)) {
        std::fprintf(stderr,
                     "HDR tone mapping: input is %ux%u, expected %dx%d\n",
                     desc.Width, desc.Height, width_, height_);
        return nullptr;
    }

    // The cached SRV holds a COM reference to its texture, so a released
    // texture's address cannot be reused while cached (ABA-safe).
    if (cached_input_ != hdr_texture) {
        input_srv_.Reset();
        cached_input_ = nullptr;

        D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc{};
        srv_desc.Format = desc.Format;
        srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srv_desc.Texture2D.MostDetailedMip = 0;
        srv_desc.Texture2D.MipLevels = 1;
        const HRESULT hr = device_->CreateShaderResourceView(
            hdr_texture, &srv_desc, &input_srv_);
        if (FAILED(hr)) {
            std::fprintf(stderr, "%s:%d HRESULT=0x%08lx\n", __FILE__, __LINE__,
                         static_cast<unsigned long>(hr));
            return nullptr;
        }
        cached_input_ = hdr_texture;
    }
    if (!input_srv_) {
        return nullptr;
    }

    const Constants constants{203.0f, 1000.0f, pq_mode_ ? 1 : 0, 0};
    context_->UpdateSubresource(constants_.Get(), 0, nullptr, &constants, 0, 0);

    D3D11_VIEWPORT viewport{};
    viewport.Width = static_cast<float>(width_);
    viewport.Height = static_cast<float>(height_);
    viewport.MaxDepth = 1.0f;
    context_->RSSetViewports(1, &viewport);

    ID3D11RenderTargetView* rtv = output_rtv_.Get();
    context_->OMSetRenderTargets(1, &rtv, nullptr);
    ID3D11ShaderResourceView* srvs[] = {input_srv_.Get()};
    context_->PSSetShaderResources(0, 1, srvs);
    ID3D11SamplerState* samplers[] = {sampler_.Get()};
    context_->PSSetSamplers(0, 1, samplers);
    ID3D11Buffer* cbs[] = {constants_.Get()};
    context_->PSSetConstantBuffers(0, 1, cbs);
    context_->VSSetShader(vertex_shader_.Get(), nullptr, 0);
    context_->PSSetShader(pixel_shader_.Get(), nullptr, 0);
    context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context_->Draw(3, 0);

    // Leave the output usable as a normal shader/video-processor input.
    ID3D11RenderTargetView* no_rtv = nullptr;
    context_->OMSetRenderTargets(1, &no_rtv, nullptr);
    ID3D11ShaderResourceView* no_srv = nullptr;
    context_->PSSetShaderResources(0, 1, &no_srv);

    return output_.Get();
}

}  // namespace hmmob
