#include "core/preview/DesktopPreview.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <exception>
#include <future>
#include <mutex>
#include <thread>
#include <utility>

#include <objbase.h>

#include "core/capture/DxgiCapture.h"
#include "core/gfx/D3D11Device.h"
#include "core/gfx/VideoProcessor.h"

namespace hmmob {
namespace {

// Resolves the Start() handshake even if Run() unwinds through an exception.
class InitPublisher {
public:
    explicit InitPublisher(std::promise<bool>& promise) : promise_(promise) {}
    InitPublisher(const InitPublisher&) = delete;
    InitPublisher& operator=(const InitPublisher&) = delete;

    ~InitPublisher() {
        if (!resolved_) {
            try {
                promise_.set_value(false);
            } catch (...) {
            }
        }
    }

    void Resolve(bool ok) {
        promise_.set_value(ok);
        resolved_ = true;
    }

private:
    std::promise<bool>& promise_;
    bool resolved_ = false;
};

int64_t NowUs() {
    return std::chrono::duration_cast<std::chrono::microseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

}  // namespace

struct DesktopPreview::Impl {
    int display_index = 0;
    int width = 0;
    int height = 0;
    int fps = 30;

    std::thread thread;
    std::promise<bool> init_promise;

    // `error` and `latest_frame` are published under `mutex` so the UI can
    // pull them from another thread.
    mutable std::mutex mutex;
    std::string error;
    std::shared_ptr<const PreviewFrame> latest_frame;
    uint64_t sequence = 0;

    std::atomic<bool> stop_requested{false};
    std::atomic<bool> running{false};

    void Run();

    // Keeps the first failure; a later consequence must not mask its cause.
    void SetError(std::string message) {
        std::lock_guard<std::mutex> lock(mutex);
        if (error.empty()) {
            error = std::move(message);
        }
    }
};

void DesktopPreview::Impl::Run() {
    // This thread owns the D3D/DXGI objects; the guard destroys after the
    // pipeline locals so CoUninitialize runs once they are gone.
    const HRESULT com_result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    struct ComScope {
        HRESULT result;
        ~ComScope() {
            if (SUCCEEDED(result)) {
                CoUninitialize();
            }
        }
    } com_scope{com_result};
    (void)com_scope;

    InitPublisher init(init_promise);

    const auto fail_init = [&](std::string message) {
        SetError(std::move(message));
        running.store(false);
        init.Resolve(false);
    };

    D3D11Device d3d;
    if (!d3d.Create()) {
        fail_init("failed to create d3d11 device");
        return;
    }

    const auto displays = d3d.EnumerateDisplays();
    if (displays.empty()) {
        fail_init("no display found");
        return;
    }
    if (display_index < 0 ||
        display_index >= static_cast<int>(displays.size())) {
        fail_init("display index out of range");
        return;
    }

    DxgiCapture capture;
    if (!capture.Initialize(d3d, display_index, /*request_hdr=*/false)) {
        fail_init("failed to initialize dxgi capture");
        return;
    }

    VideoProcessor converter;
    if (!converter.Initialize(d3d, capture.width(), capture.height(), width,
                              height, DXGI_FORMAT_B8G8R8A8_UNORM)) {
        fail_init("failed to initialize preview converter");
        return;
    }

    D3D11_TEXTURE2D_DESC staging_desc{};
    staging_desc.Width = static_cast<UINT>(width);
    staging_desc.Height = static_cast<UINT>(height);
    staging_desc.MipLevels = 1;
    staging_desc.ArraySize = 1;
    staging_desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    staging_desc.SampleDesc.Count = 1;
    staging_desc.Usage = D3D11_USAGE_STAGING;
    staging_desc.BindFlags = 0;
    staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> staging;
    if (FAILED(d3d.device()->CreateTexture2D(&staging_desc, nullptr,
                                             &staging))) {
        fail_init("failed to create preview staging texture");
        return;
    }

    const int64_t frame_interval_us =
        fps > 0 ? 1'000'000 / fps : 1'000'000 / 30;
    int64_t next_deadline_us = NowUs();
    // Each distinct failure is reported at most once; retries stay silent.
    bool capture_error_logged = false;
    bool conversion_error_logged = false;
    bool readback_error_logged = false;

    running.store(true);
    init.Resolve(true);

    while (!stop_requested.load()) {
        const int64_t now_us = NowUs();
        if (now_us < next_deadline_us) {
            const int64_t sleep_us =
                std::min<int64_t>(next_deadline_us - now_us, 1000);
            std::this_thread::sleep_for(std::chrono::microseconds(sleep_us));
            continue;
        }

        CaptureFrame frame;
        if (!capture.Acquire(frame, 100)) {
            if (capture.failed()) {
                SetError("capture failed");
                if (!capture_error_logged) {
                    std::fprintf(stderr,
                                 "warning: desktop preview capture failed "
                                 "(0x%08lx); keeping the last frame\n",
                                 static_cast<unsigned long>(
                                     capture.last_error()));
                    capture_error_logged = true;
                }
                // Back off instead of spinning on a dead duplication.
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
            }
            // A plain timeout means the desktop is unchanged. The first
            // acquire after duplication creation normally delivers the
            // current desktop, so there is nothing to republish here.
            continue;
        }

        if (!converter.Convert(frame.texture.Get(),
                               converter.output_texture())) {
            SetError("preview conversion failed");
            if (!conversion_error_logged) {
                std::fprintf(stderr,
                             "warning: desktop preview conversion failed; "
                             "keeping the last frame\n");
                conversion_error_logged = true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            continue;
        }

        d3d.context()->CopyResource(staging.Get(),
                                    converter.output_texture());

        auto preview = std::make_shared<PreviewFrame>();
        preview->width = width;
        preview->height = height;
        preview->stride = width * 4;
        preview->bgra.resize(static_cast<size_t>(preview->stride) *
                             static_cast<size_t>(preview->height));

        D3D11_MAPPED_SUBRESOURCE mapped{};
        const HRESULT map_hr = d3d.context()->Map(staging.Get(), 0,
                                                  D3D11_MAP_READ, 0, &mapped);
        if (FAILED(map_hr)) {
            SetError("preview readback failed");
            if (!readback_error_logged) {
                std::fprintf(stderr,
                             "warning: desktop preview readback failed "
                             "(0x%08lx); keeping the last frame\n",
                             static_cast<unsigned long>(map_hr));
                readback_error_logged = true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            continue;
        }

        const size_t row_bytes = static_cast<size_t>(preview->stride);
        for (int y = 0; y < preview->height; ++y) {
            std::memcpy(
                preview->bgra.data() + static_cast<size_t>(y) * row_bytes,
                static_cast<const uint8_t*>(mapped.pData) +
                    static_cast<size_t>(y) * mapped.RowPitch,
                row_bytes);
        }
        d3d.context()->Unmap(staging.Get(), 0);

        preview->sequence = ++sequence;
        {
            std::lock_guard<std::mutex> lock(mutex);
            latest_frame = std::move(preview);
        }

        next_deadline_us += frame_interval_us;
        const int64_t after_us = NowUs();
        if (after_us > next_deadline_us + frame_interval_us) {
            // A short stall should not cause two publishes back-to-back.
            next_deadline_us = after_us + frame_interval_us;
        }
    }

    running.store(false);
}

DesktopPreview::DesktopPreview() : impl_(std::make_unique<Impl>()) {}

DesktopPreview::~DesktopPreview() { Stop(); }

bool DesktopPreview::Start(int display_index, int width, int height, int fps,
                           std::string& error) {
    // A previous run must be over before its promise and thread are reused.
    Stop();

    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        impl_->error.clear();
        impl_->latest_frame.reset();
    }
    impl_->sequence = 0;
    impl_->display_index = display_index;
    impl_->width = width;
    impl_->height = height;
    impl_->fps = fps;
    impl_->stop_requested.store(false);
    impl_->running.store(false);

    impl_->init_promise = std::promise<bool>();
    std::future<bool> ready = impl_->init_promise.get_future();
    impl_->thread = std::thread([this] {
        try {
            impl_->Run();
        } catch (const std::exception& exception) {
            impl_->SetError(exception.what());
            impl_->running.store(false);
        } catch (...) {
            impl_->SetError("desktop preview thread failed");
            impl_->running.store(false);
        }
    });

    if (!ready.get()) {
        if (impl_->thread.joinable()) {
            impl_->thread.join();
        }
        std::lock_guard<std::mutex> lock(impl_->mutex);
        error = impl_->error;
        return false;
    }
    return true;
}

void DesktopPreview::Stop() {
    impl_->stop_requested.store(true);
    if (impl_->thread.joinable()) {
        impl_->thread.join();
    }
    impl_->running.store(false);
}

bool DesktopPreview::Running() const { return impl_->running.load(); }

std::string DesktopPreview::error() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->error;
}

std::shared_ptr<const PreviewFrame> DesktopPreview::LatestFrame() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->latest_frame;
}

}  // namespace hmmob
