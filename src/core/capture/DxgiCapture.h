#pragma once
#include "core/common/QpcClock.h"
#include "core/gfx/D3D11Device.h"
#include <dxgi1_5.h>
#include <wrl/client.h>
#include <cstdint>

namespace hmmob {

struct CaptureFrame {
    // Reused by the next Acquire; consume before calling Acquire again.
    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
    int64_t pts_us = 0;
    int width = 0;
    int height = 0;
    bool hdr = false;
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
};

class DxgiCapture {
public:
    bool Initialize(D3D11Device& d3d, int display_index, bool request_hdr);
    bool Acquire(CaptureFrame& frame, int timeout_ms);
    void Shutdown();

    int width() const { return width_; }
    int height() const { return height_; }
    DXGI_FORMAT format() const { return format_; }
    bool hdr() const { return hdr_; }
    bool access_lost() const { return access_lost_; }

private:
    bool CreateDuplication();

    D3D11Device* d3d_ = nullptr;
    int display_index_ = 0;
    bool request_hdr_ = false;
    bool access_lost_ = false;

    Microsoft::WRL::ComPtr<IDXGIOutput1> output1_;
    Microsoft::WRL::ComPtr<IDXGIOutputDuplication> duplication_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> copy_texture_;

    int width_ = 0;
    int height_ = 0;
    bool hdr_ = false;
    DXGI_FORMAT format_ = DXGI_FORMAT_B8G8R8A8_UNORM;
    QpcClock clock_;
};

}  // namespace hmmob
