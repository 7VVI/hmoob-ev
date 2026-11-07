#pragma once
#include "core/audio/PcmRingBuffer.h"
#include <windows.h>
#include <atomic>
#include <memory>

namespace hmmob {

class WasapiCapture {
public:
    // Defined in the .cpp so Impl can stay incomplete for unique_ptr.
    WasapiCapture();
    ~WasapiCapture();
    WasapiCapture(const WasapiCapture&) = delete;
    WasapiCapture& operator=(const WasapiCapture&) = delete;

    // loopback=true: default render endpoint (system sound);
    // loopback=false: default microphone. Output is 48k stereo float32.
    [[nodiscard]] bool Initialize(bool loopback, int sample_rate, int channels);
    [[nodiscard]] bool Start();
    void Stop();
    void Shutdown();
    // Copies up to `frames` interleaved float frames; returns frames copied.
    int Read(float* interleaved, int frames);
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
