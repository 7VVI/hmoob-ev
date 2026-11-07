#pragma once
#include "core/audio/PcmRingBuffer.h"
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
    bool is_running() const { return running_; }

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::unique_ptr<PcmRingBuffer> ring_;
    int sample_rate_ = 48000;
    int channels_ = 2;
    bool running_ = false;
};

}  // namespace hmmob
