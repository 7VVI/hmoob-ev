#include "core/capture/WindowSource.h"

#include <d3d11_4.h>
#include <roapi.h>
#include <windows.graphics.directx.direct3d11.interop.h>
#include <Windows.Graphics.Capture.Interop.h>
#include <wrl/wrappers/corewrappers.h>

#include <chrono>
#include <cstdio>
#include <thread>
#include <utility>

namespace hmmob {
namespace {

using Microsoft::WRL::ComPtr;
using Microsoft::WRL::Wrappers::HStringReference;

namespace WGC = ABI::Windows::Graphics::Capture;
namespace WGDX = ABI::Windows::Graphics::DirectX;
namespace WGD3D = ABI::Windows::Graphics::DirectX::Direct3D11;

constexpr char kWindowClosed[] = "capture window closed";
constexpr char kWindowResized[] =
    "capture window size changed; stop and restart recording";

std::string HresultMessage(const char* what, HRESULT hr) {
    char buffer[192];
    std::snprintf(buffer, sizeof(buffer), "%s (0x%08lx)", what,
                  static_cast<unsigned long>(hr));
    return buffer;
}

// Closes a WinRT object implementing IClosable. Objects that do not implement
// it (or fail to close) are still released by the ComPtr, which stops capture
// as well.
void CloseWinrtObject(IUnknown* object) {
    if (!object) {
        return;
    }
    ComPtr<ABI::Windows::Foundation::IClosable> closable;
    if (SUCCEEDED(object->QueryInterface(IID_PPV_ARGS(&closable)))) {
        closable->Close();
    }
}

}  // namespace

WindowSource::~WindowSource() { Close(); }

void WindowSource::RecordFailure(std::string message, HRESULT hr) {
    if (last_message_.empty()) {
        last_message_ = std::move(message);
        last_error_ = hr;
    }
    failed_ = true;
}

void WindowSource::Close() {
    // The session holds the item and the pool; close the session first so no
    // frame is delivered while the rest is torn down.
    if (session_) {
        CloseWinrtObject(session_.Get());
        session_.Reset();
    }
    if (pool_) {
        CloseWinrtObject(pool_.Get());
        pool_.Reset();
    }
    item_.Reset();
    texture_.Reset();
    context_.Reset();
    device_.Reset();
    window_ = nullptr;
    width_ = 0;
    height_ = 0;
    window_width_ = 0;
    window_height_ = 0;
    if (ro_initialized_) {
        RoUninitialize();
        ro_initialized_ = false;
    }
}

bool WindowSource::CreateOwnedTexture() {
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = static_cast<UINT>(window_width_);
    desc.Height = static_cast<UINT>(window_height_);
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    // CreateVideoProcessorInputView requires a render-target texture; the
    // recorder feeds this texture straight into its BGRA8 -> NV12 converter.
    desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    const HRESULT hr = device_->CreateTexture2D(
        &desc, nullptr, texture_.ReleaseAndGetAddressOf());
    if (FAILED(hr)) {
        last_error_ = hr;
        return false;
    }
    return true;
}

bool WindowSource::Initialize(D3D11Device& d3d, const RecorderConfig& cfg,
                              std::string& error) {
    Close();
    failed_ = false;
    last_error_ = S_OK;
    last_message_.clear();

    window_ = static_cast<HWND>(cfg.window);
    if (!window_) {
        error = "no window selected";
        return false;
    }
    if (!IsWindow(window_)) {
        error = kWindowClosed;
        return false;
    }
    if (!d3d.device() || !d3d.context()) {
        error = "d3d11 device is unavailable";
        window_ = nullptr;
        return false;
    }

    device_ = d3d.device();
    context_ = d3d.context();

    const auto fail = [&](std::string message) {
        error = std::move(message);
        Close();
        return false;
    };

    // Windows Graphics Capture uses the device from its own threads, while the
    // recorder still uses the immediate context on the recording thread.
    ComPtr<ID3D11Multithread> multithread;
    if (SUCCEEDED(context_.As(&multithread))) {
        multithread->SetMultithreadProtected(TRUE);
    }

    // The recording thread usually has COM initialized already (CoInitializeEx
    // MTA in Recorder); RoInitialize is reference counted, so it is safe to
    // call here and only uninitialize the reference taken here. S_FALSE means
    // the thread is already initialized; RPC_E_CHANGED_MODE means an STA, which
    // still allows activation of the free-threaded capture objects.
    const HRESULT ro_hr = RoInitialize(RO_INIT_MULTITHREADED);
    if (ro_hr == S_OK) {
        ro_initialized_ = true;
    } else if (ro_hr != S_FALSE && ro_hr != RPC_E_CHANGED_MODE) {
        return fail(
            HresultMessage("failed to initialize the windows runtime", ro_hr));
    }

    ComPtr<IGraphicsCaptureItemInterop> interop;
    HRESULT hr = RoGetActivationFactory(
        HStringReference(
            RuntimeClass_Windows_Graphics_Capture_GraphicsCaptureItem)
            .Get(),
        IID_PPV_ARGS(&interop));
    if (FAILED(hr)) {
        return fail(
            HresultMessage("windows graphics capture is unavailable", hr));
    }
    hr = interop->CreateForWindow(window_, IID_PPV_ARGS(&item_));
    if (FAILED(hr)) {
        return fail(HresultMessage("failed to capture the window", hr));
    }

    ABI::Windows::Graphics::SizeInt32 size{};
    hr = item_->get_Size(&size);
    if (FAILED(hr)) {
        return fail(HresultMessage("failed to read the window size", hr));
    }
    if (size.Width <= 0 || size.Height <= 0) {
        return fail("capture window has no visible area");
    }
    if (size.Width < 2 || size.Height < 2) {
        return fail("capture window is too small");
    }
    // The encoder path converts to NV12, whose textures need even dimensions.
    // Window rects are frequently odd, so the encoded size drops the last row
    // and/or column (at most one pixel) while the texture keeps the window
    // size and source_rect crops the encoded area.
    window_width_ = size.Width;
    window_height_ = size.Height;
    width_ = window_width_ & ~1;
    height_ = window_height_ & ~1;

    // The frame pool needs the WinRT wrapper of the D3D device, not the D3D
    // interface itself.
    ComPtr<IDXGIDevice> dxgi_device;
    hr = device_.As(&dxgi_device);
    if (FAILED(hr)) {
        return fail(HresultMessage("failed to query the dxgi device", hr));
    }
    ComPtr<IInspectable> inspectable_device;
    hr = CreateDirect3D11DeviceFromDXGIDevice(dxgi_device.Get(),
                                              &inspectable_device);
    if (FAILED(hr)) {
        return fail(HresultMessage("failed to wrap the d3d11 device", hr));
    }
    ComPtr<WGD3D::IDirect3DDevice> winrt_device;
    hr = inspectable_device.As(&winrt_device);
    if (FAILED(hr)) {
        return fail(HresultMessage("unexpected graphics device type", hr));
    }

    ComPtr<WGC::IDirect3D11CaptureFramePoolStatics2> pool_statics;
    hr = RoGetActivationFactory(
        HStringReference(
            RuntimeClass_Windows_Graphics_Capture_Direct3D11CaptureFramePool)
            .Get(),
        IID_PPV_ARGS(&pool_statics));
    if (FAILED(hr)) {
        return fail(
            HresultMessage("windows graphics capture is unavailable", hr));
    }
    // A free-threaded pool delivers frames without a message loop on this
    // thread; two buffers are enough for the copy in Acquire().
    hr = pool_statics->CreateFreeThreaded(
        winrt_device.Get(),
        static_cast<WGDX::DirectXPixelFormat>(DXGI_FORMAT_B8G8R8A8_UNORM), 2,
        size, pool_.ReleaseAndGetAddressOf());
    if (FAILED(hr)) {
        return fail(HresultMessage("failed to create the capture frame pool",
                                   hr));
    }

    hr = pool_->CreateCaptureSession(item_.Get(),
                                     session_.ReleaseAndGetAddressOf());
    if (FAILED(hr)) {
        return fail(HresultMessage("failed to create the capture session", hr));
    }

    // Both toggles are best effort: older Windows builds do not expose the
    // interfaces and the recording works with the default behavior.
    ComPtr<WGC::IGraphicsCaptureSession2> session2;
    if (SUCCEEDED(session_.As(&session2))) {
        (void)session2->put_IsCursorCaptureEnabled(static_cast<boolean>(true));
    }
    ComPtr<WGC::IGraphicsCaptureSession3> session3;
    if (SUCCEEDED(session_.As(&session3))) {
        (void)session3->put_IsBorderRequired(static_cast<boolean>(false));
    }

    hr = session_->StartCapture();
    if (FAILED(hr)) {
        return fail(HresultMessage("failed to start window capture", hr));
    }

    if (!CreateOwnedTexture()) {
        return fail(HresultMessage("failed to create the capture texture",
                                   last_error_));
    }
    return true;
}

bool WindowSource::CopyFrame(WGC::IDirect3D11CaptureFrame* capture_frame,
                             bool& copied) {
    copied = false;
    // Every path must close the frame, otherwise the pool stops delivering.
    struct FrameGuard {
        WGC::IDirect3D11CaptureFrame* frame;
        ~FrameGuard() { CloseWinrtObject(frame); }
    } guard{capture_frame};

    ABI::Windows::Graphics::SizeInt32 item_size{};
    HRESULT hr = item_->get_Size(&item_size);
    if (FAILED(hr)) {
        RecordFailure(HresultMessage("failed to read the window size", hr), hr);
        return false;
    }
    if (item_size.Width != window_width_ ||
        item_size.Height != window_height_) {
        RecordFailure(kWindowResized, E_FAIL);
        return false;
    }

    ABI::Windows::Graphics::SizeInt32 content_size{};
    hr = capture_frame->get_ContentSize(&content_size);
    if (FAILED(hr)) {
        RecordFailure(HresultMessage("failed to read the frame size", hr), hr);
        return false;
    }
    if (content_size.Width <= 0 || content_size.Height <= 0) {
        // Minimized windows can deliver an empty frame; keep the last texture.
        return true;
    }

    if (content_size.Width != window_width_ ||
        content_size.Height != window_height_) {
        RecordFailure(kWindowResized, E_FAIL);
        return false;
    }

    ComPtr<WGD3D::IDirect3DSurface> surface;
    hr = capture_frame->get_Surface(&surface);
    if (FAILED(hr)) {
        RecordFailure(HresultMessage("failed to read the frame surface", hr),
                      hr);
        return false;
    }
    ComPtr<Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>
        access;
    hr = surface.As(&access);
    if (FAILED(hr)) {
        RecordFailure(HresultMessage("unexpected frame surface type", hr), hr);
        return false;
    }
    ComPtr<ID3D11Texture2D> source_texture;
    hr = access->GetInterface(IID_PPV_ARGS(&source_texture));
    if (FAILED(hr)) {
        RecordFailure(HresultMessage("failed to open the frame texture", hr),
                      hr);
        return false;
    }

    D3D11_TEXTURE2D_DESC desc{};
    source_texture->GetDesc(&desc);
    if (desc.Width != static_cast<UINT>(window_width_) ||
        desc.Height != static_cast<UINT>(window_height_)) {
        RecordFailure(kWindowResized, E_FAIL);
        return false;
    }
    if (desc.Format != DXGI_FORMAT_B8G8R8A8_UNORM) {
        char message[128];
        std::snprintf(message, sizeof(message),
                      "unexpected capture format %d; stop and restart recording",
                      static_cast<int>(desc.Format));
        RecordFailure(message, E_FAIL);
        return false;
    }

    // WGC recycles its frames as soon as they are closed, so copy into the
    // texture owned by this source.
    context_->CopyResource(texture_.Get(), source_texture.Get());
    copied = true;
    return true;
}

bool WindowSource::Acquire(SourceFrame& frame, int timeout_ms) {
    if (failed_) {
        return false;
    }
    if (!item_ || !pool_ || !texture_) {
        RecordFailure("window capture is not initialized", E_UNEXPECTED);
        return false;
    }
    if (!window_ || !IsWindow(window_)) {
        RecordFailure(kWindowClosed,
                      HRESULT_FROM_WIN32(ERROR_INVALID_WINDOW_HANDLE));
        Close();
        return false;
    }

    const auto deadline =
        std::chrono::steady_clock::now() +
        std::chrono::milliseconds(timeout_ms > 0 ? timeout_ms : 0);
    ComPtr<WGC::IDirect3D11CaptureFrame> capture_frame;
    for (;;) {
        const HRESULT hr = pool_->TryGetNextFrame(
            capture_frame.ReleaseAndGetAddressOf());
        if (FAILED(hr)) {
            RecordFailure(HresultMessage("failed to read a captured frame", hr),
                          hr);
            Close();
            return false;
        }
        if (capture_frame) {
            break;
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    bool copied = false;
    if (capture_frame && !CopyFrame(capture_frame.Get(), copied)) {
        Close();
        return false;
    }
    if (!texture_) {
        // Timed out before the first frame arrived.
        return false;
    }

    frame.texture = texture_.Get();
    frame.width = width_;
    frame.height = height_;
    frame.source_rect = RECT{0, 0, width_, height_};
    frame.new_frame = copied;
    frame.hdr = false;
    frame.format = DXGI_FORMAT_B8G8R8A8_UNORM;
    return true;
}

}  // namespace hmmob
