#pragma once
#include "core/audio/PcmRingBuffer.h"

namespace hmmob {

class AudioMixer {
public:
    AudioMixer(int sample_rate, int channels);
    void SetSystemSource(PcmRingBuffer* source);
    void SetMicSource(PcmRingBuffer* source);
    void SetSystemGain(float gain);
    void SetMicGain(float gain);
    void SetSystemMuted(bool muted);
    void SetMicMuted(bool muted);
    // Fills `out` with exactly `frames` frames; missing source data is silence.
    void Mix(float* out, int frames);

private:
    int sample_rate_ = 48000;
    int channels_ = 2;
    PcmRingBuffer* system_ = nullptr;
    PcmRingBuffer* mic_ = nullptr;
    float system_gain_ = 1.0f;
    float mic_gain_ = 1.0f;
    bool system_muted_ = false;
    bool mic_muted_ = false;
};

}  // namespace hmmob
