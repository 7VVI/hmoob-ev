#pragma once
#include "core/gfx/D3D11Device.h"
#include <d3d11.h>
#include <wrl/client.h>

namespace hmmob {

class VideoProcessor {
public:
    // Requires a successfully created D3D11Device. Re-initialization is
    // supported: any previously created resources are released first.
    [[nodiscard]] bool Initialize(D3D11Device& d3d, int width, int height);
    // Converts into this object's output_texture(); `nv12` must equal
    // output_texture(). The parameter is kept for API symmetry and may be
    // removed in Phase 1.
    [[nodiscard]] bool Convert(ID3D11Texture2D* bgra, ID3D11Texture2D* nv12);
    ID3D11Texture2D* output_texture() const { return output_.Get(); }
    int width() const { return width_; }
    int height() const { return height_; }

private:
    void Reset();

    ID3D11Device* device_ = nullptr;
    ID3D11DeviceContext* context_ = nullptr;
    Microsoft::WRL::ComPtr<ID3D11VideoDevice> video_device_;
    Microsoft::WRL::ComPtr<ID3D11VideoContext> video_context_;
    Microsoft::WRL::ComPtr<ID3D11VideoProcessorEnumerator> enumerator_;
    Microsoft::WRL::ComPtr<ID3D11VideoProcessor> processor_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> output_;
    Microsoft::WRL::ComPtr<ID3D11VideoProcessorInputView> input_view_;
    Microsoft::WRL::ComPtr<ID3D11VideoProcessorOutputView> output_view_;
    ID3D11Texture2D* cached_input_ = nullptr;
    int width_ = 0;
    int height_ = 0;
    bool ready_ = false;
};

}  // namespace hmmob
