#include "core/audio/AudioMixer.h"
#include <algorithm>
#include <vector>

namespace hmmob {

AudioMixer::AudioMixer(int sample_rate, int channels)
    : sample_rate_(sample_rate), channels_(channels) {}

void AudioMixer::SetSystemSource(PcmRingBuffer* source) { system_ = source; }
void AudioMixer::SetMicSource(PcmRingBuffer* source) { mic_ = source; }
void AudioMixer::SetSystemGain(float gain) { system_gain_ = gain; }
void AudioMixer::SetMicGain(float gain) { mic_gain_ = gain; }
void AudioMixer::SetSystemMuted(bool muted) { system_muted_ = muted; }
void AudioMixer::SetMicMuted(bool muted) { mic_muted_ = muted; }

void AudioMixer::Mix(float* out, int frames) {
    if (!out || frames <= 0) {
        return;
    }
    const size_t samples = static_cast<size_t>(frames) * channels_;
    std::fill(out, out + samples, 0.0f);

    if (system_) {
        std::vector<float> temp(samples, 0.0f);
        const int read = system_->Read(temp.data(), frames);
        if (!system_muted_) {
            for (size_t i = 0; i < static_cast<size_t>(read) * channels_; ++i) {
                out[i] += temp[i] * system_gain_;
            }
        }
    }
    if (mic_) {
        std::vector<float> temp(samples, 0.0f);
        const int read = mic_->Read(temp.data(), frames);
        if (!mic_muted_) {
            for (size_t i = 0; i < static_cast<size_t>(read) * channels_; ++i) {
                out[i] += temp[i] * mic_gain_;
            }
        }
    }

    for (size_t i = 0; i < samples; ++i) {
        out[i] = std::clamp(out[i], -1.0f, 1.0f);
    }
}

}  // namespace hmmob
