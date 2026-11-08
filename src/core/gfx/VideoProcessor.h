#pragma once
#include "core/gfx/D3D11Device.h"
#include <d3d11.h>
#include <wrl/client.h>

namespace hmmob {

class VideoProcessor {
public:
    // Requires a successfully created D3D11Device. Re-initialization is
    // supported: any previously created resources are released first.
    // Same-size BGRA -> NV12 conversion.
    [[nodiscard]] bool Initialize(D3D11Device& d3d, int width, int height);
    // Arbitrary input/output size with an NV12 or BGRA8 output texture.
    [[nodiscard]] bool Initialize(D3D11Device& d3d, int input_width,
                                  int input_height, int output_width,
                                  int output_height,
                                  DXGI_FORMAT output_format);
    // Converts into this object's output_texture(); `output` must equal
    // output_texture(). The parameter is kept for API symmetry and may be
    // removed in Phase 1.
    [[nodiscard]] bool Convert(ID3D11Texture2D* input, ID3D11Texture2D* output);
    ID3D11Texture2D* output_texture() const { return output_.Get(); }
    int input_width() const { return input_width_; }
    int input_height() const { return input_height_; }
    int output_width() const { return output_width_; }
    int output_height() const { return output_height_; }
    DXGI_FORMAT output_format() const { return output_format_; }

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
    int input_width_ = 0;
    int input_height_ = 0;
    int output_width_ = 0;
    int output_height_ = 0;
    DXGI_FORMAT output_format_ = DXGI_FORMAT_NV12;
    bool ready_ = false;
};

}  // namespace hmmob
