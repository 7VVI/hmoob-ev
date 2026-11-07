#include "core/gfx/D3D11Device.h"
#include "core/common/Check.h"
#include <cstdlib>

namespace hmmob {

bool D3D11Device::Create() {
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
    D3D_FEATURE_LEVEL chosen{};
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
                                   flags, levels, _countof(levels),
                                   D3D11_SDK_VERSION, &device_, &chosen, &context_);
    if (FAILED(hr)) {
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
                               D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0,
                               D3D11_SDK_VERSION, &device_, &chosen, &context_);
    }
    HMMOB_HR_CHECK(hr);

    ComPtr<IDXGIDevice> dxgi_device;
    HMMOB_HR_CHECK(device_.As(&dxgi_device));
    ComPtr<IDXGIAdapter> adapter;
    HMMOB_HR_CHECK(dxgi_device->GetAdapter(&adapter));
    ComPtr<IDXGIFactory2> factory;
    HMMOB_HR_CHECK(adapter->GetParent(IID_PPV_ARGS(&factory)));
    factory_ = factory;
    return true;
}

std::vector<DisplayInfo> D3D11Device::EnumerateDisplays() const {
    std::vector<DisplayInfo> result;
    if (!device_) {
        return result;
    }

    ComPtr<IDXGIDevice> dxgi_device;
    if (FAILED(device_.As(&dxgi_device))) {
        return result;
    }
    ComPtr<IDXGIAdapter> adapter;
    if (FAILED(dxgi_device->GetAdapter(&adapter))) {
        return result;
    }

    for (UINT i = 0;; ++i) {
        ComPtr<IDXGIOutput> output;
        if (adapter->EnumOutputs(i, &output) == DXGI_ERROR_NOT_FOUND) {
            break;
        }
        if (!output) {
            break;
        }

        DisplayInfo info;
        info.index = static_cast<int>(i);

        ComPtr<IDXGIOutput6> output6;
        if (SUCCEEDED(output.As(&output6))) {
            DXGI_OUTPUT_DESC1 desc1{};
            if (SUCCEEDED(output6->GetDesc1(&desc1))) {
                info.name = desc1.DeviceName;
                info.width = desc1.DesktopCoordinates.right - desc1.DesktopCoordinates.left;
                info.height = desc1.DesktopCoordinates.bottom - desc1.DesktopCoordinates.top;
                info.color_space = desc1.ColorSpace;
                info.hdr = desc1.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020 ||
                           desc1.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709;
                result.push_back(info);
                continue;
            }
        }

        DXGI_OUTPUT_DESC desc{};
        if (SUCCEEDED(output->GetDesc(&desc))) {
            info.name = desc.DeviceName;
            info.width = desc.DesktopCoordinates.right - desc.DesktopCoordinates.left;
            info.height = desc.DesktopCoordinates.bottom - desc.DesktopCoordinates.top;
            result.push_back(info);
        }
    }
    return result;
}

}  // namespace hmmob
