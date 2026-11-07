#pragma once
#include <d3d11.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <string>
#include <vector>

namespace hmmob {

using Microsoft::WRL::ComPtr;

struct DisplayInfo {
    int index = 0;
    std::wstring name;
    int width = 0;
    int height = 0;
    bool hdr = false;
    DXGI_COLOR_SPACE_TYPE color_space = DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
};

class D3D11Device {
public:
    bool Create();
    ID3D11Device* device() const { return device_.Get(); }
    ID3D11DeviceContext* context() const { return context_.Get(); }
    IDXGIFactory2* factory() const { return factory_.Get(); }
    std::vector<DisplayInfo> EnumerateDisplays() const;

private:
    ComPtr<ID3D11Device> device_;
    ComPtr<ID3D11DeviceContext> context_;
    ComPtr<IDXGIFactory2> factory_;
};

}  // namespace hmmob
