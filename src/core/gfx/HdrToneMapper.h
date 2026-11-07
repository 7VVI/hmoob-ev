#pragma once
#include "core/gfx/D3D11Device.h"
#include <d3d11.h>
#include <wrl/client.h>

namespace hmmob {

class HdrToneMapper {
public:
    // Requires a successfully created D3D11Device. Re-initialization is
    // supported: any previously created resources are released first.
    [[nodiscard]] bool Initialize(D3D11Device& d3d, int width, int height, bool pq_mode);
    // Maps an R16G16B16A16_FLOAT or R10G10B10A2_UNORM texture to BGRA8.
    [[nodiscard]] ID3D11Texture2D* Map(ID3D11Texture2D* hdr_texture);
    ID3D11Texture2D* output_texture() const { return output_.Get(); }
    int width() const { return width_; }
    int height() const { return height_; }

private:
    void Reset();

    ID3D11Device* device_ = nullptr;
    ID3D11DeviceContext* context_ = nullptr;
    Microsoft::WRL::ComPtr<ID3D11VertexShader> vertex_shader_;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> pixel_shader_;
    Microsoft::WRL::ComPtr<ID3D11Buffer> constants_;
    Microsoft::WRL::ComPtr<ID3D11SamplerState> sampler_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> output_;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> output_rtv_;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> input_srv_;
    ID3D11Texture2D* cached_input_ = nullptr;
    int width_ = 0;
    int height_ = 0;
    bool pq_mode_ = true;
    bool ready_ = false;
};

}  // namespace hmmob
