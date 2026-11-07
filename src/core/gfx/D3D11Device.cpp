#include "core/gfx/D3D11Device.h"
#include "core/common/Check.h"
#include <cstdio>
#include <iterator>

namespace hmmob {

namespace {

int RectWidth(const RECT& rect) {
    return rect.right - rect.left;
}

int RectHeight(const RECT& rect) {
    return rect.bottom - rect.top;
}

}  // namespace

bool IsHdrColorSpace(DXGI_COLOR_SPACE_TYPE color_space) {
    return color_space == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020 ||
           color_space == DXGI_COLOR_SPACE_RGB_STUDIO_G2084_NONE_P2020 ||
           color_space == DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709;
}

DisplayInfo FromDesc1(const DXGI_OUTPUT_DESC1& desc,
                      int index, int adapter_index, int output_index) {
    DisplayInfo info;
    info.index = index;
    info.adapter_index = adapter_index;
    info.output_index = output_index;
    info.name = desc.DeviceName;
    info.width = RectWidth(desc.DesktopCoordinates);
    info.height = RectHeight(desc.DesktopCoordinates);
    info.attached_to_desktop = desc.AttachedToDesktop != FALSE;
    info.hdr = IsHdrColorSpace(desc.ColorSpace);
    info.color_space = desc.ColorSpace;
    info.rotation = desc.Rotation;
    info.max_luminance = desc.MaxLuminance;
    info.max_full_frame_luminance = desc.MaxFullFrameLuminance;
    info.bits_per_color = desc.BitsPerColor;
    return info;
}

void D3D11Device::Reset() {
    device_.Reset();
    context_.Reset();
    factory_.Reset();
    feature_level_ = static_cast<D3D_FEATURE_LEVEL>(0);
}

bool D3D11Device::Create() {
    Reset();

    const UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
    D3D_FEATURE_LEVEL chosen = static_cast<D3D_FEATURE_LEVEL>(0);
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
                                   flags, levels, static_cast<UINT>(std::size(levels)),
                                   D3D11_SDK_VERSION, &device_, &chosen, &context_);
    if (hr == E_INVALIDARG) {
        // The runtime may not understand feature level 11_1; retry with the SDK default list.
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
                               flags, nullptr, 0,
                               D3D11_SDK_VERSION, &device_, &chosen, &context_);
    }
    if (FAILED(hr) || !device_ || !context_ || chosen < D3D_FEATURE_LEVEL_11_0) {
        std::fprintf(stderr,
                     "D3D11CreateDevice failed: hr=0x%08lx feature_level=0x%04x "
                     "device=%p context=%p\n",
                     static_cast<unsigned long>(hr),
                     static_cast<unsigned int>(chosen),
                     static_cast<void*>(device_.Get()),
                     static_cast<void*>(context_.Get()));
        Reset();
        return false;
    }

    ComPtr<IDXGIDevice> dxgi_device;
    hr = device_.As(&dxgi_device);
    if (FAILED(hr)) {
        std::fprintf(stderr, "QueryInterface(IDXGIDevice) failed: 0x%08lx\n",
                     static_cast<unsigned long>(hr));
        Reset();
        return false;
    }
    ComPtr<IDXGIAdapter> adapter;
    hr = dxgi_device->GetAdapter(&adapter);
    if (FAILED(hr)) {
        std::fprintf(stderr, "IDXGIDevice::GetAdapter failed: 0x%08lx\n",
                     static_cast<unsigned long>(hr));
        Reset();
        return false;
    }
    ComPtr<IDXGIFactory2> factory;
    hr = adapter->GetParent(IID_PPV_ARGS(&factory));
    if (FAILED(hr)) {
        std::fprintf(stderr, "IDXGIAdapter::GetParent(IDXGIFactory2) failed: 0x%08lx\n",
                     static_cast<unsigned long>(hr));
        Reset();
        return false;
    }

    factory_ = factory;
    feature_level_ = chosen;
    return true;
}

std::vector<DisplayInfo> D3D11Device::EnumerateDisplays() const {
    std::vector<DisplayInfo> result;
    if (!device_ || !factory_) {
        return result;
    }

    int global_index = 0;
    for (UINT adapter_index = 0;; ++adapter_index) {
        ComPtr<IDXGIAdapter1> adapter;
        const HRESULT adapter_hr = factory_->EnumAdapters1(adapter_index, &adapter);
        if (adapter_hr == DXGI_ERROR_NOT_FOUND) {
            break;
        }
        if (FAILED(adapter_hr) || !adapter) {
            std::fprintf(stderr, "IDXGIFactory2::EnumAdapters1(%u) failed: 0x%08lx\n",
                         adapter_index, static_cast<unsigned long>(adapter_hr));
            break;
        }

        for (UINT output_index = 0;; ++output_index) {
            ComPtr<IDXGIOutput> output;
            const HRESULT output_hr = adapter->EnumOutputs(output_index, &output);
            if (output_hr == DXGI_ERROR_NOT_FOUND) {
                break;
            }
            if (FAILED(output_hr) || !output) {
                std::fprintf(stderr,
                             "IDXGIAdapter1::EnumOutputs(%u) on adapter %u failed: 0x%08lx\n",
                             output_index, adapter_index,
                             static_cast<unsigned long>(output_hr));
                break;
            }

            DisplayInfo info;
            bool have_desc1 = false;
            ComPtr<IDXGIOutput6> output6;
            if (SUCCEEDED(output.As(&output6))) {
                DXGI_OUTPUT_DESC1 desc1{};
                if (SUCCEEDED(output6->GetDesc1(&desc1))) {
                    info = FromDesc1(desc1, global_index,
                                     static_cast<int>(adapter_index),
                                     static_cast<int>(output_index));
                    have_desc1 = true;
                }
            }

            if (!have_desc1) {
                DXGI_OUTPUT_DESC desc{};
                if (FAILED(output->GetDesc(&desc))) {
                    continue;
                }
                info.index = global_index;
                info.adapter_index = static_cast<int>(adapter_index);
                info.output_index = static_cast<int>(output_index);
                info.name = desc.DeviceName;
                info.width = RectWidth(desc.DesktopCoordinates);
                info.height = RectHeight(desc.DesktopCoordinates);
                info.attached_to_desktop = desc.AttachedToDesktop != FALSE;
                info.rotation = desc.Rotation;
                // hdr, luminance, color_space, and bits_per_color keep their defaults.
            }

            // Phase 0 excludes inactive/virtual outputs that have no desktop area.
            if (!info.attached_to_desktop || info.width <= 0 || info.height <= 0) {
                continue;
            }

            result.push_back(info);
            ++global_index;
        }
    }
    return result;
}

}  // namespace hmmob
