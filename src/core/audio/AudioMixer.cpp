#include "core/audio/AudioMixer.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace hmmob {

AudioMixer::AudioMixer(int sample_rate, int channels)
    : sample_rate_(sample_rate), channels_(channels) {
    if (sample_rate_ <= 0) {
        throw std::invalid_argument("AudioMixer: sample_rate must be positive");
    }
    if (channels_ <= 0) {
        throw std::invalid_argument("AudioMixer: channels must be positive");
    }
}

void AudioMixer::SetSystemSource(PcmRingBuffer* source) { system_ = source; }
void AudioMixer::SetMicSource(PcmRingBuffer* source) { mic_ = source; }
void AudioMixer::SetSystemGain(float gain) { system_gain_ = gain; }
void AudioMixer::SetMicGain(float gain) { mic_gain_ = gain; }
void AudioMixer::SetSystemMuted(bool muted) { system_muted_ = muted; }
void AudioMixer::SetMicMuted(bool muted) { mic_muted_ = muted; }

void AudioMixer::MixSource(PcmRingBuffer* source, std::vector<float>& scratch,
                           float gain, bool muted, float* out, int frames) {
    if (!source || source->channels() != channels_) {
        // Reading a mismatched source would write source-channel samples into a
        // mixer-channel-sized scratch buffer; ignore it instead.
        return;
    }
    const size_t samples = static_cast<size_t>(frames) * static_cast<size_t>(channels_);
    scratch.assign(samples, 0.0f);
    const int read = source->Read(scratch.data(), frames);
    if (muted) {
        // Data was drained above so the producer never blocks.
        return;
    }
    for (size_t i = 0; i < static_cast<size_t>(read) * static_cast<size_t>(channels_); ++i) {
        out[i] += scratch[i] * gain;
    }
}

void AudioMixer::Mix(float* out, int frames) {
    if (!out || frames <= 0) {
        return;
    }
    const size_t samples = static_cast<size_t>(frames) * static_cast<size_t>(channels_);
    std::fill(out, out + samples, 0.0f);

    MixSource(system_, system_scratch_, system_gain_, system_muted_, out, frames);
    MixSource(mic_, mic_scratch_, mic_gain_, mic_muted_, out, frames);

    // tanh keeps the result strictly inside (-1, 1) and avoids the harsh
    // distortion of hard clipping on summed sources.
    for (size_t i = 0; i < samples; ++i) {
        out[i] = std::tanh(out[i]);
    }
}

}  // namespace hmmob
