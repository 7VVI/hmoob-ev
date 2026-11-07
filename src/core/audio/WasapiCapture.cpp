#include "core/audio/WasapiCapture.h"

#include <windows.h>
#include <audioclient.h>
#include <ksmedia.h>
#include <mmdeviceapi.h>
#include <mmreg.h>
#include <propsys.h>
#include <wrl/client.h>

extern "C" {
#include <libavutil/channel_layout.h>
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
}  // namespace

struct WasapiCapture::Impl {
    bool loopback = false;
    // True only when this thread actually initialized COM (S_OK/S_FALSE).
    // RPC_E_CHANGED_MODE means another apartment already owns it, and calling
    // CoUninitialize then would unbalance that owner's reference count.
    bool com_initialized = false;
    // Event-driven capture is the normal path; some systems reject an
    // event-driven loopback stream, in which case we poll instead.
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
    int sample_rate = 48000;
    int channels = 2;
    PcmRingBuffer* ring = nullptr;

    bool ConvertAndPush(const BYTE* data, UINT32 frames) {
        if (!ring || !data || frames == 0) {
            return true;
        }
        if (!swr) {
            ring->Write(reinterpret_cast<const float*>(data),
                        static_cast<int>(frames));
            return true;
        }
        const int64_t capacity64 =
            swr_get_out_samples(swr, static_cast<int>(frames));
        if (capacity64 < 0) {
            return false;
        }
        const int capacity = static_cast<int>(capacity64);
        if (capacity == 0) {
            return true;
        }
        std::vector<float> out(static_cast<size_t>(capacity) *
                               static_cast<size_t>(channels));
        const uint8_t* input[1] = {data};
        uint8_t* output[1] = {reinterpret_cast<uint8_t*>(out.data())};
        const int converted =
            swr_convert(swr, output, capacity, input, static_cast<int>(frames));
        if (converted < 0) {
            return false;
        }
        ring->Write(out.data(), converted);
        return true;
    }

    void Run() {
        while (running.load(std::memory_order_relaxed)) {
            if (event_driven) {
                WaitForSingleObject(event, 100);
            } else {
                Sleep(10);
            }
            if (!running.load(std::memory_order_relaxed)) {
                break;
            }
            UINT32 packet_frames = 0;
            while (running.load(std::memory_order_relaxed) &&
                   SUCCEEDED(capture->GetNextPacketSize(&packet_frames)) &&
                   packet_frames > 0) {
                BYTE* data = nullptr;
                UINT32 frames = 0;
                DWORD packet_flags = 0;
                if (FAILED(capture->GetBuffer(&data, &frames, &packet_flags,
                                              nullptr, nullptr))) {
                    break;
                }
                if (packet_flags & AUDCLNT_BUFFERFLAGS_SILENT) {
                    // Silent packets may carry no data pointer; feed explicit
                    // zeros through swr so the frame count still matches the
                    // output rate.
                    std::vector<float> zeros(
                        static_cast<size_t>(frames) *
                            static_cast<size_t>(mix_format->nChannels),
                        0.0f);
                    ConvertAndPush(reinterpret_cast<const BYTE*>(zeros.data()),
                                   frames);
                } else {
                    ConvertAndPush(data, frames);
                }
                capture->ReleaseBuffer(frames);
            }
        }
    }
};

WasapiCapture::WasapiCapture() = default;

WasapiCapture::~WasapiCapture() { Shutdown(); }

bool WasapiCapture::Initialize(bool loopback, int sample_rate, int channels) {
    Shutdown();
    if (sample_rate <= 0 || channels <= 0) {
        return false;
    }
    sample_rate_ = sample_rate;
    channels_ = channels;
    // One second of target-rate audio; the writer drops the oldest frames if
    // the consumer stalls, so this only bounds latency and memory.
    ring_ = std::make_unique<PcmRingBuffer>(channels, sample_rate);

    impl_ = std::make_unique<Impl>();
    impl_->loopback = loopback;
    impl_->sample_rate = sample_rate;
    impl_->channels = channels;
    impl_->ring = ring_.get();

    const HRESULT co = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (co == S_OK || co == S_FALSE) {
        impl_->com_initialized = true;
    } else if (co != RPC_E_CHANGED_MODE) {
        LogHr("CoInitializeEx", co);
        Shutdown();
        return false;
    }

    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr,
                                  CLSCTX_ALL, IID_PPV_ARGS(&impl_->enumerator));
    if (FAILED(hr)) {
        LogHr("CoCreateInstance(MMDeviceEnumerator)", hr);
        Shutdown();
        return false;
    }
    hr = impl_->enumerator->GetDefaultAudioEndpoint(
        loopback ? eRender : eCapture, eConsole, &impl_->device);
    if (FAILED(hr)) {
        LogHr("GetDefaultAudioEndpoint", hr);
        Shutdown();
        return false;
    }
    hr = impl_->device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                                 &impl_->client);
    if (FAILED(hr)) {
        LogHr("Activate(IAudioClient)", hr);
        Shutdown();
        return false;
    }
    hr = impl_->client->GetMixFormat(&impl_->mix_format);
    if (FAILED(hr)) {
        LogHr("GetMixFormat", hr);
        Shutdown();
        return false;
    }
    if (!IsFloat32Format(impl_->mix_format)) {
        std::fprintf(stderr,
                     "WasapiCapture: mix format tag=0x%04x bits=%u is not "
                     "32-bit float\n",
                     static_cast<unsigned>(impl_->mix_format->wFormatTag),
                     static_cast<unsigned>(impl_->mix_format->wBitsPerSample));
        Shutdown();
        return false;
    }

    DWORD stream_flags = AUDCLNT_STREAMFLAGS_EVENTCALLBACK;
    if (loopback) {
        stream_flags |= AUDCLNT_STREAMFLAGS_LOOPBACK;
    }
    hr = impl_->client->Initialize(AUDCLNT_SHAREMODE_SHARED, stream_flags, 0, 0,
                                   impl_->mix_format, nullptr);
    if (FAILED(hr) && loopback &&
        (hr == E_INVALIDARG || hr == AUDCLNT_E_EVENTHANDLE_NOT_EXPECTED)) {
        // Event-driven loopback capture is rejected on some Windows builds and
        // drivers. Re-activate a fresh client (Initialize is one-shot per
        // client) and fall back to polling.
        LogHr("IAudioClient::Initialize (event-driven loopback), retrying with "
              "polling",
              hr);
        impl_->client.Reset();
        hr = impl_->device->Activate(__uuidof(IAudioClient), CLSCTX_ALL,
                                     nullptr, &impl_->client);
        if (SUCCEEDED(hr)) {
            hr = impl_->client->Initialize(AUDCLNT_SHAREMODE_SHARED,
                                           AUDCLNT_STREAMFLAGS_LOOPBACK, 0, 0,
                                           impl_->mix_format, nullptr);
            if (SUCCEEDED(hr)) {
                impl_->event_driven = false;
            }
        }
    }
    if (FAILED(hr)) {
        LogHr("IAudioClient::Initialize", hr);
        Shutdown();
        return false;
    }
    hr = impl_->client->GetService(IID_PPV_ARGS(&impl_->capture));
    if (FAILED(hr)) {
        LogHr("GetService(IAudioCaptureClient)", hr);
        Shutdown();
        return false;
    }
    if (impl_->event_driven) {
        impl_->event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (!impl_->event) {
            LogHr("CreateEventW", HRESULT_FROM_WIN32(GetLastError()));
            Shutdown();
            return false;
        }
        hr = impl_->client->SetEventHandle(impl_->event);
        if (FAILED(hr)) {
            LogHr("SetEventHandle", hr);
            Shutdown();
            return false;
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
            return false;
        }
    }
    return true;
}

bool WasapiCapture::Start() {
    if (!impl_ || !impl_->client || running_) {
        return false;
    }
    const HRESULT hr = impl_->client->Start();
    if (FAILED(hr)) {
        LogHr("IAudioClient::Start", hr);
        return false;
    }
    running_ = true;
    impl_->running.store(true, std::memory_order_relaxed);
    try {
        impl_->thread = std::thread(&Impl::Run, impl_.get());
    } catch (...) {
        impl_->running.store(false, std::memory_order_relaxed);
        running_ = false;
        impl_->client->Stop();
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
        impl_->client->Stop();
    }
}

int WasapiCapture::Read(float* interleaved, int frames) {
    return ring_ ? ring_->Read(interleaved, frames) : 0;
}

void WasapiCapture::Shutdown() {
    Stop();
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
