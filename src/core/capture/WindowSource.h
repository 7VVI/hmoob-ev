#pragma once
#include <windows.h>
#include <windows.graphics.capture.h>

#include <d3d11.h>
#include <wrl/client.h>

#include <string>

#include "core/capture/VideoSource.h"
#include "core/gfx/D3D11Device.h"

namespace hmmob {

// Windows Graphics Capture backed source for one top-level window. Frames are
// copied into a texture owned by this source so the returned pointer stays
// valid until the next Acquire(). Capture is SDR BGRA8 at the window size.
//
// The window size is fixed for the lifetime of the recording: the encoder
// cannot change resolution mid-stream, so a resize or a closed window ends the
// recording with an error instead of producing a broken file.
class WindowSource final : public VideoSource {
public:
    WindowSource() = default;
    ~WindowSource() override;

    WindowSource(const WindowSource&) = delete;
    WindowSource& operator=(const WindowSource&) = delete;

    bool Initialize(D3D11Device& d3d, const RecorderConfig& cfg,
                    std::string& error) override;
    bool Acquire(SourceFrame& frame, int timeout_ms) override;

    // Encoded size: the window size rounded down to even dimensions because
    // the NV12 encoder path cannot handle odd sizes.
    int width() const override { return width_; }
    int height() const override { return height_; }
    // Size of the captured window texture, which may be one pixel larger than
    // the encoded size on an odd axis.
    int texture_width() const override { return window_width_; }
    int texture_height() const override { return window_height_; }
    DXGI_FORMAT format() const override { return DXGI_FORMAT_B8G8R8A8_UNORM; }
    DXGI_COLOR_SPACE_TYPE color_space() const override {
        return DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
    }
    bool hdr_capable() const override { return false; }
    bool failed() const override { return failed_; }
    HRESULT last_error() const override { return last_error_; }
    std::string last_message() const override { return last_message_; }

private:
    // Releases the frame pool, the capture session and the WinRT apartment.
    // Safe to call more than once.
    void Close();
    // Copies the next captured frame into texture_. Records the failure when
    // the window went away, changed size or the frame cannot be read.
    // `copied` is false for an empty frame (a minimized window).
    bool CopyFrame(ABI::Windows::Graphics::Capture::IDirect3D11CaptureFrame*
                       capture_frame,
                   bool& copied);
    void RecordFailure(std::string message, HRESULT hr);
    [[nodiscard]] bool CreateOwnedTexture();

    Microsoft::WRL::ComPtr<ID3D11Device> device_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture_;
    Microsoft::WRL::ComPtr<ABI::Windows::Graphics::Capture::IGraphicsCaptureItem>
        item_;
    Microsoft::WRL::ComPtr<
        ABI::Windows::Graphics::Capture::IDirect3D11CaptureFramePool>
        pool_;
    Microsoft::WRL::ComPtr<
        ABI::Windows::Graphics::Capture::IGraphicsCaptureSession>
        session_;

    HWND window_ = nullptr;
    int width_ = 0;
    int height_ = 0;
    int window_width_ = 0;
    int window_height_ = 0;
    bool ro_initialized_ = false;
    bool failed_ = false;
    HRESULT last_error_ = S_OK;
    std::string last_message_;
};

}  // namespace hmmob
