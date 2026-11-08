#include <catch2/catch_test_macros.hpp>
#include <windows.h>

#include <chrono>
#include <string>
#include <thread>

#include "core/capture/WindowSource.h"
#include "core/gfx/D3D11Device.h"

namespace {

constexpr wchar_t kWindowClass[] = L"hmmob-wgc-test-window";
constexpr int kWindowWidth = 480;
constexpr int kWindowHeight = 270;

// WGC reports sizes in physical pixels; make GetClientRect() agree so the test
// works on a scaled display too.
class DpiAwarenessScope {
public:
    DpiAwarenessScope() {
        const HMODULE user32 = GetModuleHandleW(L"user32.dll");
        if (user32) {
            set_ = reinterpret_cast<SetContextFn>(
                GetProcAddress(user32, "SetThreadDpiAwarenessContext"));
        }
        if (set_) {
            previous_ = set_(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        }
    }
    ~DpiAwarenessScope() {
        if (set_ && previous_) {
            set_(previous_);
        }
    }

    DpiAwarenessScope(const DpiAwarenessScope&) = delete;
    DpiAwarenessScope& operator=(const DpiAwarenessScope&) = delete;

private:
    using SetContextFn = DPI_AWARENESS_CONTEXT(WINAPI*)(DPI_AWARENESS_CONTEXT);

    SetContextFn set_ = nullptr;
    DPI_AWARENESS_CONTEXT previous_ = nullptr;
};

struct WindowGuard {
    HWND window = nullptr;
    ~WindowGuard() {
        if (window && IsWindow(window)) {
            DestroyWindow(window);
        }
    }
};

// A borderless top-level window, so its client rect is exactly the requested
// size and matches what WGC captures.
HWND CreateTestWindow(int width = kWindowWidth, int height = kWindowHeight) {
    const HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = instance;
    wc.lpszClassName = kWindowClass;
    // Both tests register the class; already-registered is fine.
    RegisterClassExW(&wc);

    HWND window = CreateWindowExW(0, kWindowClass, L"hmmob window capture test",
                                  WS_POPUP | WS_VISIBLE, 100, 100, width,
                                  height, nullptr, nullptr, instance, nullptr);
    if (window) {
        ShowWindow(window, SW_SHOWNOACTIVATE);
        UpdateWindow(window);
        // Give DWM a moment to compose the window before capture starts.
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    return window;
}

}  // namespace

// Requires a desktop session: D3D11 plus Windows Graphics Capture (Windows 10
// 2004 or newer for the free-threaded frame pool).
TEST_CASE("WindowSource captures a real window", "[integration]") {
    DpiAwarenessScope dpi;
    hmmob::D3D11Device d3d;
    if (!d3d.Create()) {
        SKIP("d3d11 unavailable");
    }

    HWND window = CreateTestWindow();
    REQUIRE(window != nullptr);
    WindowGuard guard{window};

    RECT client{};
    REQUIRE(GetClientRect(window, &client));
    const int expected_width = client.right - client.left;
    const int expected_height = client.bottom - client.top;
    REQUIRE(expected_width > 0);
    REQUIRE(expected_height > 0);

    hmmob::RecorderConfig config;
    config.mode = hmmob::CaptureMode::Window;
    config.window = window;

    hmmob::WindowSource source;
    std::string error;
    if (!source.Initialize(d3d, config, error)) {
        SKIP("WGC unavailable: " + error);
    }

    REQUIRE(source.width() == expected_width);
    REQUIRE(source.height() == expected_height);
    REQUIRE(source.format() == DXGI_FORMAT_B8G8R8A8_UNORM);
    REQUIRE(source.color_space() == DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709);
    REQUIRE_FALSE(source.hdr_capable());

    hmmob::SourceFrame frame;
    bool captured = false;
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (std::chrono::steady_clock::now() < deadline) {
        if (source.Acquire(frame, 100)) {
            captured = true;
            break;
        }
        if (source.failed()) {
            break;
        }
    }
    INFO("capture error: " + source.last_message());
    REQUIRE(captured);

    REQUIRE(frame.texture != nullptr);
    REQUIRE(frame.width == expected_width);
    REQUIRE(frame.height == expected_height);
    REQUIRE(frame.format == DXGI_FORMAT_B8G8R8A8_UNORM);
    REQUIRE_FALSE(frame.hdr);
    REQUIRE(frame.new_frame);
    REQUIRE(frame.source_rect.left == 0);
    REQUIRE(frame.source_rect.top == 0);
    REQUIRE(frame.source_rect.right == expected_width);
    REQUIRE(frame.source_rect.bottom == expected_height);

    D3D11_TEXTURE2D_DESC desc{};
    frame.texture->GetDesc(&desc);
    REQUIRE(desc.Width == static_cast<UINT>(expected_width));
    REQUIRE(desc.Height == static_cast<UINT>(expected_height));
    REQUIRE(desc.Format == DXGI_FORMAT_B8G8R8A8_UNORM);

    // Without a new frame the source repeats its owned texture so the recorder
    // can keep encoding at a fixed rate.
    ID3D11Texture2D* first_texture = frame.texture;
    hmmob::SourceFrame repeated;
    REQUIRE(source.Acquire(repeated, 50));
    REQUIRE(repeated.texture == first_texture);
    REQUIRE(repeated.width == expected_width);
    REQUIRE(repeated.height == expected_height);
}

// WGC ends the capture when the window is destroyed; the source must report it
// instead of delivering the last frame forever.
TEST_CASE("WindowSource reports a closed window", "[integration]") {
    DpiAwarenessScope dpi;
    hmmob::D3D11Device d3d;
    if (!d3d.Create()) {
        SKIP("d3d11 unavailable");
    }

    HWND window = CreateTestWindow();
    REQUIRE(window != nullptr);
    WindowGuard guard{window};

    hmmob::RecorderConfig config;
    config.mode = hmmob::CaptureMode::Window;
    config.window = window;

    hmmob::WindowSource source;
    std::string error;
    if (!source.Initialize(d3d, config, error)) {
        SKIP("WGC unavailable: " + error);
    }

    REQUIRE(DestroyWindow(window));
    guard.window = nullptr;

    hmmob::SourceFrame frame;
    bool acquired = true;
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (std::chrono::steady_clock::now() < deadline) {
        acquired = source.Acquire(frame, 100);
        if (!acquired) {
            break;
        }
    }
    REQUIRE_FALSE(acquired);
    REQUIRE(source.failed());
    REQUIRE(source.last_error() != S_OK);
    INFO("capture error: " + source.last_message());
    REQUIRE(source.last_message().find("closed") != std::string::npos);
}

// Window rects are often odd sized while the NV12 encoder path needs even
// dimensions: the texture keeps the window size and source_rect crops the
// encoded area.
TEST_CASE("WindowSource rounds an odd window size to an even output",
          "[integration]") {
    DpiAwarenessScope dpi;
    hmmob::D3D11Device d3d;
    if (!d3d.Create()) {
        SKIP("d3d11 unavailable");
    }

    HWND window = CreateTestWindow(481, 271);
    REQUIRE(window != nullptr);
    WindowGuard guard{window};

    RECT client{};
    REQUIRE(GetClientRect(window, &client));
    REQUIRE(client.right - client.left == 481);
    REQUIRE(client.bottom - client.top == 271);

    hmmob::RecorderConfig config;
    config.mode = hmmob::CaptureMode::Window;
    config.window = window;

    hmmob::WindowSource source;
    std::string error;
    if (!source.Initialize(d3d, config, error)) {
        SKIP("WGC unavailable: " + error);
    }

    REQUIRE(source.texture_width() == 481);
    REQUIRE(source.texture_height() == 271);
    REQUIRE(source.width() == 480);
    REQUIRE(source.height() == 270);

    hmmob::SourceFrame frame;
    bool captured = false;
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (std::chrono::steady_clock::now() < deadline) {
        if (source.Acquire(frame, 100)) {
            captured = true;
            break;
        }
        if (source.failed()) {
            break;
        }
    }
    INFO("capture error: " + source.last_message());
    REQUIRE(captured);
    REQUIRE(frame.width == 480);
    REQUIRE(frame.height == 270);
    REQUIRE(frame.source_rect.left == 0);
    REQUIRE(frame.source_rect.top == 0);
    REQUIRE(frame.source_rect.right == 480);
    REQUIRE(frame.source_rect.bottom == 270);

    D3D11_TEXTURE2D_DESC desc{};
    frame.texture->GetDesc(&desc);
    REQUIRE(desc.Width == 481);
    REQUIRE(desc.Height == 271);
}
