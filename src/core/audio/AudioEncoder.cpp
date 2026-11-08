#include "core/audio/AudioEncoder.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstring>

namespace hmmob {
namespace {

constexpr int kPcmFrameSize = 1024;
constexpr int kAacMinBitrate = 96000;
constexpr int kAacMaxBitrate = 320000;

bool IsSupportedSampleRate(int sample_rate) {
    return sample_rate == 44100 || sample_rate == 48000;
}

bool IsSupportedChannels(int channels) {
    return channels == 1 || channels == 2;
}

// float [-1, 1] -> signed 16-bit. Out-of-range input (and NaN) is clamped or
// zeroed so the conversion is total, and halves are rounded away from zero.
int16_t FloatToS16(float sample) {
    if (!std::isfinite(sample)) {
        return 0;
    }
    const float clamped = std::clamp(sample, -1.0f, 1.0f);
    return static_cast<int16_t>(std::lround(clamped * 32767.0f));
}

// Writes one interleaved s16le sample pair/group into `out` at sample offset.
void StoreS16le(uint8_t* out, size_t sample_index, int16_t value) {
    const auto unsigned_value = static_cast<uint16_t>(value);
    out[sample_index * 2] = static_cast<uint8_t>(unsigned_value & 0xFF);
    out[sample_index * 2 + 1] = static_cast<uint8_t>(unsigned_value >> 8);
}

}  // namespace

struct AudioEncoder::Impl {
    AudioCodec codec = AudioCodec::Aac;
    int sample_rate = 0;
    int channels = 0;
    int bitrate = 0;
    // AAC backend; owned only for AudioCodec::Aac.
    AacEncoder aac;
    // PCM samples (interleaved s16 host values) not yet emitted.
    std::vector<int16_t> pending;
    int64_t next_pts_samples = 0;
    bool flushed = false;

    void EmitPcm(size_t sample_offset, int frames, const PacketCallback& callback) {
        const size_t samples = static_cast<size_t>(frames) *
                               static_cast<size_t>(channels);
        AacPacket packet;
        packet.pts_samples = next_pts_samples;
        packet.duration_samples = frames;
        packet.data.resize(samples * 2);
        for (size_t i = 0; i < samples; ++i) {
            StoreS16le(packet.data.data(), i, pending[sample_offset + i]);
        }
        next_pts_samples += frames;
        callback(packet);
    }
};

AudioEncoder::~AudioEncoder() { Shutdown(); }

bool AudioEncoder::Initialize(int sample_rate, int channels, int bitrate,
                              AudioCodec codec) {
    Shutdown();
    if (!IsSupportedSampleRate(sample_rate)) {
        std::fprintf(stderr,
                     "AudioEncoder: sample rate %d is not supported; use "
                     "44100 or 48000\n",
                     sample_rate);
        return false;
    }
    if (!IsSupportedChannels(channels)) {
        std::fprintf(stderr,
                     "AudioEncoder: %d channels is not supported; use mono or "
                     "stereo\n",
                     channels);
        return false;
    }
    if (codec == AudioCodec::Aac &&
        (bitrate < kAacMinBitrate || bitrate > kAacMaxBitrate)) {
        std::fprintf(stderr,
                     "AudioEncoder: AAC bitrate %d is outside 96000..320000\n",
                     bitrate);
        return false;
    }

    impl_ = new Impl();
    impl_->codec = codec;
    impl_->sample_rate = sample_rate;
    impl_->channels = channels;
    impl_->bitrate = bitrate;

    if (codec == AudioCodec::Aac) {
        if (!impl_->aac.Initialize(sample_rate, channels, bitrate)) {
            Shutdown();
            return false;
        }
    }
    return true;
}

bool AudioEncoder::Encode(const float* interleaved, int frames,
                          const PacketCallback& callback) {
    if (!impl_ || impl_->flushed || !callback || frames < 0) {
        return false;
    }
    if (frames == 0) {
        // Nothing to encode; an empty input is a benign no-op.
        return true;
    }
    if (!interleaved) {
        return false;
    }
    if (impl_->codec == AudioCodec::Aac) {
        return impl_->aac.Encode(interleaved, frames, callback);
    }

    const size_t samples = static_cast<size_t>(frames) *
                           static_cast<size_t>(impl_->channels);
    impl_->pending.reserve(impl_->pending.size() + samples);
    for (size_t i = 0; i < samples; ++i) {
        impl_->pending.push_back(FloatToS16(interleaved[i]));
    }

    const size_t frame_size = static_cast<size_t>(kPcmFrameSize);
    const size_t samples_per_packet = frame_size *
                                      static_cast<size_t>(impl_->channels);
    size_t offset = 0;
    while (impl_->pending.size() - offset >= samples_per_packet) {
        impl_->EmitPcm(offset, kPcmFrameSize, callback);
        offset += samples_per_packet;
    }
    if (offset > 0) {
        impl_->pending.erase(impl_->pending.begin(),
                             impl_->pending.begin() +
                                 static_cast<std::ptrdiff_t>(offset));
    }
    return true;
}

bool AudioEncoder::Flush(const PacketCallback& callback) {
    if (!impl_ || impl_->flushed || !callback) {
        return false;
    }
    if (impl_->codec == AudioCodec::Aac) {
        if (!impl_->aac.Flush(callback)) {
            return false;
        }
        impl_->flushed = true;
        return true;
    }

    if (!impl_->pending.empty()) {
        const int remaining = static_cast<int>(
            impl_->pending.size() / static_cast<size_t>(impl_->channels));
        impl_->EmitPcm(0, remaining, callback);
        impl_->pending.clear();
    }
    impl_->flushed = true;
    return true;
}

void AudioEncoder::Shutdown() {
    if (!impl_) {
        return;
    }
    impl_->aac.Shutdown();
    delete impl_;
    impl_ = nullptr;
}

bool AudioEncoder::is_initialized() const {
    if (!impl_) {
        return false;
    }
    return impl_->codec == AudioCodec::PcmS16Le ||
           impl_->aac.is_initialized();
}

int AudioEncoder::sample_rate() const {
    return impl_ ? impl_->sample_rate : 0;
}

int AudioEncoder::channels() const {
    return impl_ ? impl_->channels : 0;
}

int AudioEncoder::bitrate() const {
    if (!impl_) {
        return 0;
    }
    if (impl_->codec == AudioCodec::Aac) {
        return impl_->aac.bitrate();
    }
    return impl_->sample_rate * impl_->channels * 16;
}

int AudioEncoder::frame_size() const {
    if (!impl_) {
        return 0;
    }
    if (impl_->codec == AudioCodec::Aac) {
        return impl_->aac.frame_size();
    }
    return kPcmFrameSize;
}

int AudioEncoder::initial_padding_samples() const {
    if (!impl_ || impl_->codec != AudioCodec::Aac) {
        return 0;
    }
    return impl_->aac.initial_padding_samples();
}

const std::vector<uint8_t>& AudioEncoder::extradata() const {
    static const std::vector<uint8_t> empty;
    if (!impl_ || impl_->codec != AudioCodec::Aac) {
        return empty;
    }
    return impl_->aac.extradata();
}

AudioCodec AudioEncoder::codec() const {
    return impl_ ? impl_->codec : AudioCodec::Aac;
}

}  // namespace hmmob
