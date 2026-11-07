#pragma once
#include <d3d11.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <string>
#include <vector>

namespace hmmob {

using Microsoft::WRL::ComPtr;

struct DisplayInfo {
    int index = 0;  // globally unique across adapters
    int adapter_index = 0;
    int output_index = 0;
    std::wstring name;
    int width = 0;
    int height = 0;
    bool attached_to_desktop = false;
    bool hdr = false;
    DXGI_COLOR_SPACE_TYPE color_space = DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
    DXGI_MODE_ROTATION rotation = DXGI_MODE_ROTATION_IDENTITY;
    float max_luminance = 0.0f;
    float max_full_frame_luminance = 0.0f;
    UINT bits_per_color = 0;
};

[[nodiscard]] bool IsHdrColorSpace(DXGI_COLOR_SPACE_TYPE color_space);

[[nodiscard]] DisplayInfo FromDesc1(const DXGI_OUTPUT_DESC1& desc,
                                    int index, int adapter_index, int output_index);

class D3D11Device {
public:
    [[nodiscard]] bool Create();
    ID3D11Device* device() const { return device_.Get(); }
    ID3D11DeviceContext* context() const { return context_.Get(); }
    IDXGIFactory2* factory() const { return factory_.Get(); }
    D3D_FEATURE_LEVEL feature_level() const { return feature_level_; }
    [[nodiscard]] std::vector<DisplayInfo> EnumerateDisplays() const;

private:
    void Reset();

    ComPtr<ID3D11Device> device_;
    ComPtr<ID3D11DeviceContext> context_;
    ComPtr<IDXGIFactory2> factory_;
    D3D_FEATURE_LEVEL feature_level_ = static_cast<D3D_FEATURE_LEVEL>(0);
};

}  // namespace hmmob
