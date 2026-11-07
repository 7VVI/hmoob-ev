#pragma once
#include <cstddef>
#include <vector>

#include "core/audio/PcmRingBuffer.h"

namespace hmmob {

// Sums two interleaved float32 PCM sources and applies a tanh soft limiter.
//
// Not thread-safe: call the setters and Mix from the same recording thread.
// Gains and mute should be configured before recording starts. Sources are
// non-owning pointers and must outlive the mixer. A source whose channel count
// differs from the mixer's channel count is ignored rather than read, because
// PcmRingBuffer::Read writes samples using the source's own channel count.
//
// Throws std::invalid_argument if sample_rate or channels is not positive.
class AudioMixer {
public:
    AudioMixer(int sample_rate, int channels);

    AudioMixer(const AudioMixer&) = delete;
    AudioMixer& operator=(const AudioMixer&) = delete;
    AudioMixer(AudioMixer&&) = delete;
    AudioMixer& operator=(AudioMixer&&) = delete;

    void SetSystemSource(PcmRingBuffer* source);
    void SetMicSource(PcmRingBuffer* source);
    void SetSystemGain(float gain);
    void SetMicGain(float gain);
    void SetSystemMuted(bool muted);
    void SetMicMuted(bool muted);
    // Fills `out` with exactly `frames` frames; missing source data is silence.
    // Muted sources are still drained so their producers never stall.
    void Mix(float* out, int frames);

private:
    void MixSource(PcmRingBuffer* source, std::vector<float>& scratch, float gain,
                   bool muted, float* out, int frames);

    // Reserved for future resampling / timestamp bookkeeping; not used yet.
    int sample_rate_ = 48000;
    int channels_ = 2;
    PcmRingBuffer* system_ = nullptr;
    PcmRingBuffer* mic_ = nullptr;
    float system_gain_ = 1.0f;
    float mic_gain_ = 1.0f;
    bool system_muted_ = false;
    bool mic_muted_ = false;
    // Reused between Mix calls to avoid per-call heap allocation.
    std::vector<float> system_scratch_;
    std::vector<float> mic_scratch_;
};

}  // namespace hmmob
