#include "core/capture/DxgiCapture.h"
#include "core/common/Check.h"

namespace hmmob {

bool DxgiCapture::Initialize(D3D11Device& d3d, int display_index, bool request_hdr) {
    d3d_ = &d3d;
    display_index_ = display_index;
    request_hdr_ = request_hdr;
    return CreateDuplication();
}

void DxgiCapture::ResetState() {
    duplication_.Reset();
    output1_.Reset();
    copy_texture_.Reset();
    width_ = 0;
    height_ = 0;
    hdr_ = false;
    format_ = DXGI_FORMAT_B8G8R8A8_UNORM;
    color_space_ = DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
    has_desktop_frame_ = false;
}

bool DxgiCapture::CreateDuplication() {
    ResetState();
    if (!CreateDuplicationImpl()) {
        ResetState();
        return false;
    }
    access_lost_ = false;
    return true;
}

bool DxgiCapture::CreateDuplicationImpl() {
    if (!d3d_) {
        return false;
    }
    const auto displays = d3d_->EnumerateDisplays();
    if (display_index_ < 0 || display_index_ >= static_cast<int>(displays.size())) {
        return false;
    }
    const auto& display = displays[display_index_];
    // Phase 0: the D3D11 device must be on the display's adapter;
    // display 0 is on the default adapter.
    ComPtr<IDXGIAdapter1> adapter;
    HMMOB_HR_CHECK(d3d_->factory()->EnumAdapters1(
        static_cast<UINT>(display.adapter_index), &adapter));
    ComPtr<IDXGIOutput> output;
    HMMOB_HR_CHECK(adapter->EnumOutputs(
        static_cast<UINT>(display.output_index), &output));
    HMMOB_HR_CHECK(output.As(&output1_));
    color_space_ = display.color_space;

    ComPtr<IDXGIOutput5> output5;
    if (SUCCEEDED(output.As(&output5))) {
        DXGI_FORMAT formats[3];
        UINT count = 0;
        if (request_hdr_) {
            formats[count++] = DXGI_FORMAT_R16G16B16A16_FLOAT;
            formats[count++] = DXGI_FORMAT_R10G10B10A2_UNORM;
        }
        formats[count++] = DXGI_FORMAT_B8G8R8A8_UNORM;

        HRESULT hr = output5->DuplicateOutput1(d3d_->device(), 0, count, formats, &duplication_);
        if (FAILED(hr)) {
            duplication_.Reset();
        }
    }

    if (!duplication_) {
        HMMOB_HR_CHECK(output1_->DuplicateOutput(d3d_->device(), &duplication_));
    }

    DXGI_OUTDUPL_DESC dupl_desc{};
    duplication_->GetDesc(&dupl_desc);
    width_ = static_cast<int>(dupl_desc.ModeDesc.Width);
    height_ = static_cast<int>(dupl_desc.ModeDesc.Height);
    format_ = dupl_desc.ModeDesc.Format;
    hdr_ = format_ == DXGI_FORMAT_R16G16B16A16_FLOAT ||
           format_ == DXGI_FORMAT_R10G10B10A2_UNORM;

    D3D11_TEXTURE2D_DESC tex_desc{};
    tex_desc.Width = static_cast<UINT>(width_);
    tex_desc.Height = static_cast<UINT>(height_);
    tex_desc.MipLevels = 1;
    tex_desc.ArraySize = 1;
    tex_desc.Format = format_;
    tex_desc.SampleDesc.Count = 1;
    tex_desc.Usage = D3D11_USAGE_DEFAULT;
    tex_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    HMMOB_HR_CHECK(d3d_->device()->CreateTexture2D(&tex_desc, nullptr, &copy_texture_));
    return true;
}

bool DxgiCapture::Acquire(CaptureFrame& frame, int timeout_ms) {
    if (!duplication_) {
        if (!CreateDuplication()) {
            return false;
        }
    }

    DXGI_OUTDUPL_FRAME_INFO info{};
    ComPtr<IDXGIResource> resource;
    HRESULT hr = duplication_->AcquireNextFrame(static_cast<UINT>(timeout_ms), &info, &resource);
    if (hr == DXGI_ERROR_WAIT_TIMEOUT) {
        return false;
    }
    if (hr == DXGI_ERROR_ACCESS_LOST) {
        access_lost_ = true;
        ResetState();
        return false;
    }
    HMMOB_HR_CHECK(hr);

    const bool has_new_desktop = info.LastPresentTime.QuadPart != 0;
    if (has_new_desktop) {
        ComPtr<ID3D11Texture2D> texture;
        if (FAILED(resource.As(&texture))) {
            duplication_->ReleaseFrame();
            return false;
        }
        D3D11_TEXTURE2D_DESC src_desc{};
        texture->GetDesc(&src_desc);
        if (src_desc.Format != format_ ||
            src_desc.Width != static_cast<UINT>(width_) ||
            src_desc.Height != static_cast<UINT>(height_)) {
            duplication_->ReleaseFrame();
            duplication_.Reset();
            return false;
        }
        d3d_->context()->CopyResource(copy_texture_.Get(), texture.Get());
        has_desktop_frame_ = true;
    }
    duplication_->ReleaseFrame();

    if (!has_desktop_frame_) {
        return false;  // pointer-only update before any desktop image
    }

    frame.texture = copy_texture_;
    frame.pts_us = has_new_desktop
        ? clock_.TicksToUs(info.LastPresentTime.QuadPart)
        : clock_.NowUs();
    frame.width = width_;
    frame.height = height_;
    frame.hdr = hdr_;
    frame.format = format_;
    return true;
}

void DxgiCapture::Shutdown() {
    ResetState();
    access_lost_ = false;
}

}  // namespace hmmob
