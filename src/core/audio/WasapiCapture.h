#pragma once
#include "core/audio/PcmRingBuffer.h"
#include <windows.h>
#include <atomic>
#include <memory>
#include <string>
#include <vector>

namespace hmmob {

// Captures one WASAPI endpoint (render loopback or microphone, either the
// flow's default or a specific enumerated device) on a background thread and
// writes float32 frames at the requested rate/channels into a PcmRingBuffer.
//
// Threading: Initialize, Start, Stop and Shutdown must be called from the
// controlling thread; is_running()/failed() are meant to be read there too.
// Read() is safe to call concurrently with the capture thread.
//
// Lifetime: ring() is invalidated by Initialize and Shutdown. A mixer that
// holds the ring pointer must be detached before either call, and a capture
// must never be re-Initialized while a mixer still points at its old ring.
//
// Phase 1 limitation: shared-mode endpoints must expose 32-bit float. A PCM
// endpoint is rejected with a diagnostic (no silent audio, no S16/S24
// conversion); callers should disable that source and warn.
class WasapiCapture {
public:
    // One active endpoint as reported by EnumerateDevices. id is the WASAPI
    // endpoint id (the string IMMDevice::GetId returns) and both strings are
    // UTF-8. is_default marks the eConsole default endpoint of that flow.
    struct AudioDeviceInfo {
        std::string id;
        std::string name;
        bool is_default = false;
    };

    // Defined in the .cpp so Impl can stay incomplete for unique_ptr.
    WasapiCapture();
    ~WasapiCapture();
    WasapiCapture(const WasapiCapture&) = delete;
    WasapiCapture& operator=(const WasapiCapture&) = delete;

    // Lists the active endpoints of one flow: loopback=true enumerates render
    // devices (system sound), loopback=false capture devices (microphones).
    // Initializes COM only if this thread has no apartment yet, so it is safe
    // to call from any thread; returns an empty vector when enumeration fails.
    static std::vector<AudioDeviceInfo> EnumerateDevices(bool loopback);

    // loopback=true: render endpoint (system sound); loopback=false: capture
    // endpoint (microphone). An empty device_id selects the default endpoint
    // of that flow; a non-empty one must name an endpoint returned by
    // EnumerateDevices and is used verbatim (no silent fallback to default).
    // Output is interleaved float32 at sample_rate/channels regardless of the
    // endpoint's mix format.
    [[nodiscard]] bool Initialize(bool loopback, int sample_rate, int channels,
                                  const std::string& device_id = {});
    [[nodiscard]] bool Start();
    void Stop();
    void Shutdown();
    // Copies up to `frames` interleaved float frames; returns frames copied.
    [[nodiscard]] int Read(float* interleaved, int frames);
    PcmRingBuffer* ring() { return ring_.get(); }
    int sample_rate() const { return sample_rate_; }
    int channels() const { return channels_; }
    // running_ && !failed(): the capture thread exits on its own after a
    // device/conversion error, so callers must also consult failed().
    bool is_running() const;
    // Sticky runtime failure of the capture thread (WASAPI or conversion
    // error). Reset by Initialize and Shutdown. A failed Initialize leaves
    // this false and reports its cause through last_error() instead.
    bool failed() const;
    // Most recent failure: the Initialize HRESULT, or the capture thread's
    // HRESULT (E_FAIL for swr conversion errors). S_OK when nothing failed.
    HRESULT last_error() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::unique_ptr<PcmRingBuffer> ring_;
    std::atomic<bool> failed_{false};
    std::atomic<long> last_error_{S_OK};
    int sample_rate_ = 48000;
    int channels_ = 2;
    bool running_ = false;
};

}  // namespace hmmob
