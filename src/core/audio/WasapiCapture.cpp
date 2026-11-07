#include "core/audio/WasapiCapture.h"

#include <windows.h>
#include <audioclient.h>
#include <avrt.h>
#include <ksmedia.h>
#include <mmdeviceapi.h>
#include <mmreg.h>
#include <wrl/client.h>

extern "C" {
#include <libavutil/channel_layout.h>
#include <libavutil/error.h>
#include <libavutil/samplefmt.h>
#include <libswresample/swresample.h>
}

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <thread>
#include <vector>

namespace hmmob {
namespace {
using Microsoft::WRL::ComPtr;

void LogHr(const char* what, HRESULT hr) {
    std::fprintf(stderr, "WasapiCapture: %s failed 0x%08lx\n", what,
                 static_cast<unsigned long>(hr));
}

void LogAv(const char* what, int error) {
    char message[AV_ERROR_MAX_STRING_SIZE] = {};
    av_strerror(error, message, sizeof(message));
    std::fprintf(stderr, "WasapiCapture: %s failed: %s (%d)\n", what, message,
                 error);
}

// Shared-mode endpoints should always hand out 32-bit float, but a device in
// a non-default configuration may not; refuse it rather than reinterpret bits.
bool IsFloat32Format(const WAVEFORMATEX* format) {
    if (format->wBitsPerSample != 32) {
        return false;
    }
    if (format->wFormatTag == WAVE_FORMAT_IEEE_FLOAT) {
        return true;
    }
    if (format->wFormatTag == WAVE_FORMAT_EXTENSIBLE &&
        format->cbSize >=
            sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX)) {
        const auto* extensible =
            reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(format);
        return IsEqualGUID(extensible->SubFormat,
                           KSDATAFORMAT_SUBTYPE_IEEE_FLOAT) != FALSE;
    }
    return false;
}

// Human-readable encoding family for diagnostics. For WAVE_FORMAT_EXTENSIBLE
// the tag alone says nothing, so inspect the subformat GUID.
const char* FormatFamily(const WAVEFORMATEX* format) {
    if (format->wFormatTag == WAVE_FORMAT_IEEE_FLOAT) {
        return "float";
    }
    if (format->wFormatTag == WAVE_FORMAT_PCM) {
        return "PCM";
    }
    if (format->wFormatTag == WAVE_FORMAT_EXTENSIBLE &&
        format->cbSize >=
            sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX)) {
        const auto* extensible =
            reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(format);
        if (IsEqualGUID(extensible->SubFormat,
                        KSDATAFORMAT_SUBTYPE_IEEE_FLOAT) != FALSE) {
            return "float";
        }
        if (IsEqualGUID(extensible->SubFormat, KSDATAFORMAT_SUBTYPE_PCM) !=
            FALSE) {
            return "PCM";
        }
    }
    return "unknown";
}
}  // namespace

struct WasapiCapture::Impl {
    // True only when this thread actually initialized COM (S_OK/S_FALSE).
    // RPC_E_CHANGED_MODE means another apartment already owns it, and calling
    // CoUninitialize then would unbalance that owner's reference count.
    bool com_initialized = false;
    // Event-driven capture is the normal path; some systems/drivers reject
    // EVENTCALLBACK (loopback or microphone), in which case we poll instead.
    bool event_driven = true;
    ComPtr<IMMDeviceEnumerator> enumerator;
    ComPtr<IMMDevice> device;
    ComPtr<IAudioClient> client;
    ComPtr<IAudioCaptureClient> capture;
    WAVEFORMATEX* mix_format = nullptr;
    HANDLE event = nullptr;
    std::thread thread;
    std::atomic<bool> running{false};
    SwrContext* swr = nullptr;
    int channels = 2;
    PcmRingBuffer* ring = nullptr;
    // Owned by WasapiCapture; let the capture thread report failures without
    // reaching into the object's private members.
    std::atomic<bool>* failed = nullptr;
    std::atomic<long>* last_error = nullptr;

    bool HasFailed() const {
        return failed && failed->load(std::memory_order_relaxed);
    }

    // Records the first failure only, so a broken device does not spam the log
    // once per 100 ms wait. Only the capture thread calls this.
    void Fail(const char* what, HRESULT hr) {
        if (!failed || failed->load(std::memory_order_relaxed)) {
            return;
        }
        // Publish the cause before the flag: a reader that observes
        // failed()==true (acquire) must also observe the matching error.
        last_error->store(static_cast<long>(hr), std::memory_order_relaxed);
        failed->store(true, std::memory_order_release);
        LogHr(what, hr);
    }

    HRESULT ConvertAndPush(const BYTE* data, UINT32 frames) {
        if (!ring || !data || frames == 0) {
            return S_OK;
        }
        if (!swr) {
            ring->Write(reinterpret_cast<const float*>(data),
                        static_cast<int>(frames));
            return S_OK;
        }
        const int64_t capacity64 =
            swr_get_out_samples(swr, static_cast<int>(frames));
        if (capacity64 < 0) {
            LogAv("swr_get_out_samples", static_cast<int>(capacity64));
            return E_FAIL;
        }
        const int capacity = static_cast<int>(capacity64);
        if (capacity == 0) {
            return S_OK;
        }
        std::vector<float> out(static_cast<size_t>(capacity) *
                               static_cast<size_t>(channels));
        const uint8_t* input[1] = {data};
        uint8_t* output[1] = {reinterpret_cast<uint8_t*>(out.data())};
        const int converted =
            swr_convert(swr, output, capacity, input, static_cast<int>(frames));
        if (converted < 0) {
            LogAv("swr_convert", converted);
            return E_FAIL;
        }
        ring->Write(out.data(), converted);
        return S_OK;
    }

    void Run() {
        // COM apartments are per-thread; the thread that calls Initialize does
        // not initialize this one. Only undo it if this call created it.
        const HRESULT co = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        const bool thread_com_initialized = (co == S_OK || co == S_FALSE);
        if (FAILED(co) && co != RPC_E_CHANGED_MODE) {
            Fail("CoInitializeEx(capture thread)", co);
            return;
        }
        // Opt into the "Audio" real-time task so MMCSS schedules this thread
        // with the audio work class. Failure is not fatal (e.g. RDP sessions).
        DWORD task_index = 0;
        HANDLE mm_task = AvSetMmThreadCharacteristicsW(L"Audio", &task_index);
        if (!mm_task) {
            LogHr("AvSetMmThreadCharacteristicsW",
                  HRESULT_FROM_WIN32(GetLastError()));
        }
        try {
            while (running.load(std::memory_order_relaxed) && !HasFailed()) {
                if (event_driven) {
                    WaitForSingleObject(event, 100);
                } else {
                    Sleep(10);
                }
                if (!running.load(std::memory_order_relaxed) || HasFailed()) {
                    break;
                }
                for (;;) {
                    if (!running.load(std::memory_order_relaxed) ||
                        HasFailed()) {
                        break;
                    }
                    UINT32 packet_frames = 0;
                    HRESULT hr = capture->GetNextPacketSize(&packet_frames);
                    if (FAILED(hr)) {
                        Fail("GetNextPacketSize", hr);
                        break;
                    }
                    if (packet_frames == 0) {
                        break;
                    }
                    BYTE* data = nullptr;
                    UINT32 frames = 0;
                    DWORD packet_flags = 0;
                    hr = capture->GetBuffer(&data, &frames, &packet_flags,
                                            nullptr, nullptr);
                    if (FAILED(hr)) {
                        Fail("GetBuffer", hr);
                        break;
                    }
                    HRESULT pushed = S_OK;
                    if (packet_flags & AUDCLNT_BUFFERFLAGS_SILENT) {
                        // Silent packets may carry no data pointer; feed
                        // explicit zeros through swr so the frame count still
                        // matches the output rate.
                        std::vector<float> zeros(
                            static_cast<size_t>(frames) *
                                static_cast<size_t>(mix_format->nChannels),
                            0.0f);
                        pushed = ConvertAndPush(
                            reinterpret_cast<const BYTE*>(zeros.data()), frames);
                    } else {
                        pushed = ConvertAndPush(data, frames);
                    }
                    // The packet must be released even when conversion failed.
                    const HRESULT released = capture->ReleaseBuffer(frames);
                    if (FAILED(released)) {
                        Fail("ReleaseBuffer", released);
                        break;
                    }
                    if (FAILED(pushed)) {
                        Fail("ConvertAndPush", pushed);
                        break;
                    }
                }
            }
        } catch (...) {
            // Vector allocation failure inside ConvertAndPush. Let Stop() join
            // this thread normally instead of terminating the process.
            Fail("capture thread exception", E_FAIL);
        }
        if (HasFailed() && client) {
            // Stop the engine promptly instead of leaving it running until the
            // controlling thread notices failed() and calls Stop(). The main
            // thread's Stop() only runs after join(), so these calls never
            // overlap.
            const HRESULT stop_hr = client->Stop();
            if (FAILED(stop_hr)) {
                LogHr("IAudioClient::Stop (failure path)", stop_hr);
            }
        }
        if (mm_task) {
            AvRevertMmThreadCharacteristics(mm_task);
        }
        if (thread_com_initialized) {
            CoUninitialize();
        }
    }
};

WasapiCapture::WasapiCapture() = default;

WasapiCapture::~WasapiCapture() { Shutdown(); }

bool WasapiCapture::Initialize(bool loopback, int sample_rate, int channels) {
    Shutdown();
    const auto fail_init = [this](HRESULT hr) {
        last_error_.store(static_cast<long>(hr), std::memory_order_relaxed);
        return false;
    };
    if (sample_rate <= 0 || channels <= 0) {
        return fail_init(E_INVALIDARG);
    }
    sample_rate_ = sample_rate;
    channels_ = channels;
    // One second of target-rate audio; the writer drops the oldest frames if
    // the consumer stalls, so this only bounds latency and memory.
    ring_ = std::make_unique<PcmRingBuffer>(channels, sample_rate);

    impl_ = std::make_unique<Impl>();
    impl_->channels = channels;
    impl_->ring = ring_.get();
    impl_->failed = &failed_;
    impl_->last_error = &last_error_;

    const HRESULT co = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (co == S_OK || co == S_FALSE) {
        impl_->com_initialized = true;
    } else if (co != RPC_E_CHANGED_MODE) {
        LogHr("CoInitializeEx", co);
        Shutdown();
        return fail_init(co);
    }

    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr,
                                  CLSCTX_ALL, IID_PPV_ARGS(&impl_->enumerator));
    if (FAILED(hr)) {
        LogHr("CoCreateInstance(MMDeviceEnumerator)", hr);
        Shutdown();
        return fail_init(hr);
    }
    hr = impl_->enumerator->GetDefaultAudioEndpoint(
        loopback ? eRender : eCapture, eConsole, &impl_->device);
    if (FAILED(hr)) {
        LogHr("GetDefaultAudioEndpoint", hr);
        Shutdown();
        return fail_init(hr);
    }
    hr = impl_->device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                                 &impl_->client);
    if (FAILED(hr)) {
        LogHr("Activate(IAudioClient)", hr);
        Shutdown();
        return fail_init(hr);
    }
    hr = impl_->client->GetMixFormat(&impl_->mix_format);
    if (FAILED(hr)) {
        LogHr("GetMixFormat", hr);
        Shutdown();
        return fail_init(hr);
    }
    if (!IsFloat32Format(impl_->mix_format)) {
        // Phase 1 limitation: no PCM/other-format conversion. Report exactly
        // what the endpoint exposes so the caller can warn and disable it.
        std::fprintf(stderr,
                     "WasapiCapture: endpoint mix format is %s%u (tag 0x%04x, "
                     "%u ch, %lu Hz); Phase 1 supports 32-bit float only, "
                     "capture disabled\n",
                     FormatFamily(impl_->mix_format),
                     static_cast<unsigned>(impl_->mix_format->wBitsPerSample),
                     static_cast<unsigned>(impl_->mix_format->wFormatTag),
                     static_cast<unsigned>(impl_->mix_format->nChannels),
                     static_cast<unsigned long>(
                         impl_->mix_format->nSamplesPerSec));
        Shutdown();
        return fail_init(E_FAIL);
    }

    const DWORD endpoint_flags =
        loopback ? AUDCLNT_STREAMFLAGS_LOOPBACK : 0;
    hr = impl_->client->Initialize(AUDCLNT_SHAREMODE_SHARED,
                                   AUDCLNT_STREAMFLAGS_EVENTCALLBACK |
                                       endpoint_flags,
                                   0, 0, impl_->mix_format, nullptr);
    if (FAILED(hr) && (hr == E_INVALIDARG ||
                       hr == AUDCLNT_E_EVENTHANDLE_NOT_EXPECTED)) {
        // Some Windows builds and drivers reject event-driven capture, for
        // loopback streams and some microphones alike. Re-activate a fresh
        // client (Initialize is one-shot per client) and poll instead.
        LogHr("IAudioClient::Initialize (event-driven), retrying with polling",
              hr);
        impl_->client.Reset();
        hr = impl_->device->Activate(__uuidof(IAudioClient), CLSCTX_ALL,
                                     nullptr, &impl_->client);
        if (FAILED(hr)) {
            LogHr("Activate(IAudioClient) fallback", hr);
            Shutdown();
            return fail_init(hr);
        }
        hr = impl_->client->Initialize(AUDCLNT_SHAREMODE_SHARED, endpoint_flags,
                                       0, 0, impl_->mix_format, nullptr);
        if (FAILED(hr)) {
            LogHr("IAudioClient::Initialize (polling fallback)", hr);
            Shutdown();
            return fail_init(hr);
        }
        impl_->event_driven = false;
    } else if (FAILED(hr)) {
        LogHr("IAudioClient::Initialize", hr);
        Shutdown();
        return fail_init(hr);
    }
    hr = impl_->client->GetService(IID_PPV_ARGS(&impl_->capture));
    if (FAILED(hr)) {
        LogHr("GetService(IAudioCaptureClient)", hr);
        Shutdown();
        return fail_init(hr);
    }
    if (impl_->event_driven) {
        impl_->event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (!impl_->event) {
            const HRESULT event_hr = HRESULT_FROM_WIN32(GetLastError());
            LogHr("CreateEventW", event_hr);
            Shutdown();
            return fail_init(event_hr);
        }
        hr = impl_->client->SetEventHandle(impl_->event);
        if (FAILED(hr)) {
            LogHr("SetEventHandle", hr);
            Shutdown();
            return fail_init(hr);
        }
    }

    const bool needs_resample =
        impl_->mix_format->nSamplesPerSec != static_cast<DWORD>(sample_rate) ||
        impl_->mix_format->nChannels != static_cast<WORD>(channels);
    if (needs_resample) {
        AVChannelLayout in_layout;
        AVChannelLayout out_layout;
        av_channel_layout_default(&in_layout, impl_->mix_format->nChannels);
        av_channel_layout_default(&out_layout, channels);
        const int result = swr_alloc_set_opts2(
            &impl_->swr, &out_layout, AV_SAMPLE_FMT_FLT, sample_rate,
            &in_layout, AV_SAMPLE_FMT_FLT,
            static_cast<int>(impl_->mix_format->nSamplesPerSec), 0, nullptr);
        av_channel_layout_uninit(&in_layout);
        av_channel_layout_uninit(&out_layout);
        if (result < 0 || swr_init(impl_->swr) < 0) {
            std::fprintf(stderr,
                         "WasapiCapture: cannot convert mix format %lu Hz/%u ch "
                         "to %d Hz/%d ch\n",
                         static_cast<unsigned long>(
                             impl_->mix_format->nSamplesPerSec),
                         static_cast<unsigned>(impl_->mix_format->nChannels),
                         sample_rate, channels);
            Shutdown();
            return fail_init(E_FAIL);
        }
    }
    return true;
}

bool WasapiCapture::Start() {
    if (!impl_ || !impl_->client || running_ || failed()) {
        return false;
    }
    const HRESULT hr = impl_->client->Start();
    if (FAILED(hr)) {
        LogHr("IAudioClient::Start", hr);
        last_error_.store(static_cast<long>(hr), std::memory_order_relaxed);
        return false;
    }
    running_ = true;
    impl_->running.store(true, std::memory_order_relaxed);
    if (impl_->event) {
        ResetEvent(impl_->event);
    }
    try {
        impl_->thread = std::thread(&Impl::Run, impl_.get());
    } catch (...) {
        impl_->running.store(false, std::memory_order_relaxed);
        running_ = false;
        impl_->client->Stop();
        last_error_.store(static_cast<long>(E_FAIL), std::memory_order_relaxed);
        return false;
    }
    return true;
}

void WasapiCapture::Stop() {
    if (!impl_) {
        return;
    }
    running_ = false;
    impl_->running.store(false, std::memory_order_relaxed);
    if (impl_->event) {
        SetEvent(impl_->event);
    }
    if (impl_->thread.joinable()) {
        impl_->thread.join();
    }
    if (impl_->client) {
        // Stop() is a no-op on a stream that was never started, but a failure
        // here can still indicate a torn-down endpoint worth reporting.
        const HRESULT hr = impl_->client->Stop();
        if (FAILED(hr)) {
            LogHr("IAudioClient::Stop", hr);
        }
    }
}

int WasapiCapture::Read(float* interleaved, int frames) {
    return ring_ ? ring_->Read(interleaved, frames) : 0;
}

bool WasapiCapture::is_running() const { return running_ && !failed(); }

bool WasapiCapture::failed() const {
    // Acquire pairs with the release store in Impl::Fail, so a caller that
    // sees true can read the matching last_error().
    return failed_.load(std::memory_order_acquire);
}

HRESULT WasapiCapture::last_error() const {
    return static_cast<HRESULT>(last_error_.load(std::memory_order_relaxed));
}

void WasapiCapture::Shutdown() {
    Stop();
    // Clear failure state only after the capture thread has been joined.
    failed_.store(false, std::memory_order_relaxed);
    last_error_.store(S_OK, std::memory_order_relaxed);
    if (!impl_) {
        ring_.reset();
        return;
    }
    const bool com_initialized = impl_->com_initialized;
    if (impl_->swr) {
        swr_free(&impl_->swr);
    }
    if (impl_->event) {
        CloseHandle(impl_->event);
        impl_->event = nullptr;
    }
    if (impl_->mix_format) {
        CoTaskMemFree(impl_->mix_format);
        impl_->mix_format = nullptr;
    }
    // Release every COM interface before uninitializing the apartment.
    impl_->capture.Reset();
    impl_->client.Reset();
    impl_->device.Reset();
    impl_->enumerator.Reset();
    impl_.reset();
    ring_.reset();
    if (com_initialized) {
        CoUninitialize();
    }
}

}  // namespace hmmob
