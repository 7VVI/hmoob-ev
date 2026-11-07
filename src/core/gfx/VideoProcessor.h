#pragma once
#include "core/gfx/D3D11Device.h"
#include <d3d11.h>
#include <wrl/client.h>

namespace hmmob {

class VideoProcessor {
public:
    [[nodiscard]] bool Initialize(D3D11Device& d3d, int width, int height);
    [[nodiscard]] bool Convert(ID3D11Texture2D* bgra, ID3D11Texture2D* nv12);
    ID3D11Texture2D* output_texture() const { return output_.Get(); }

private:
    ID3D11Device* device_ = nullptr;
    ID3D11DeviceContext* context_ = nullptr;
    Microsoft::WRL::ComPtr<ID3D11VideoDevice> video_device_;
    Microsoft::WRL::ComPtr<ID3D11VideoContext> video_context_;
    Microsoft::WRL::ComPtr<ID3D11VideoProcessorEnumerator> enumerator_;
    Microsoft::WRL::ComPtr<ID3D11VideoProcessor> processor_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> output_;
    int width_ = 0;
    int height_ = 0;
};

}  // namespace hmmob
