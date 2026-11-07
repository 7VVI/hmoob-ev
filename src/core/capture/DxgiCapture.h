#pragma once
#include "core/common/QpcClock.h"
#include "core/gfx/D3D11Device.h"
#include <dxgi1_5.h>
#include <wrl/client.h>
#include <cstdint>

namespace hmmob {

struct CaptureFrame {
    // Shared texture: contents are overwritten by the next Acquire.
    // Do not enqueue or retain across Acquire; Phase 1 adds a texture pool.
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
    // True when the last Acquire/Initialize hit an unrecoverable acquisition
    // error (device removed, format mismatch, duplication creation failure).
    // A WAIT_TIMEOUT or ACCESS_LOST is not a failure.
    bool failed() const { return failed_; }
    HRESULT last_error() const { return last_error_; }
    DXGI_COLOR_SPACE_TYPE color_space() const { return color_space_; }
    bool hdr_requested() const { return request_hdr_; }
    bool hdr_downgraded() const { return request_hdr_ && !hdr_; }

private:
    bool CreateDuplication();
    bool CreateDuplicationImpl();
    void ResetState();

    D3D11Device* d3d_ = nullptr;
    int display_index_ = 0;
    bool request_hdr_ = false;
    bool access_lost_ = false;
    bool failed_ = false;
    HRESULT last_error_ = S_OK;
    bool has_desktop_frame_ = false;

    Microsoft::WRL::ComPtr<IDXGIOutput1> output1_;
    Microsoft::WRL::ComPtr<IDXGIOutputDuplication> duplication_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> copy_texture_;

    int width_ = 0;
    int height_ = 0;
    bool hdr_ = false;
    DXGI_FORMAT format_ = DXGI_FORMAT_B8G8R8A8_UNORM;
    // PQ vs scRGB cannot be inferred from the format alone; consumers combine
    // color_space() with format(). Task 10 owns that mapping.
    DXGI_COLOR_SPACE_TYPE color_space_ = DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
    QpcClock clock_;
};

}  // namespace hmmob
