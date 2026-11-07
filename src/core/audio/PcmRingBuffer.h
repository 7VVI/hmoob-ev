#pragma once
#include <cstddef>
#include <mutex>
#include <stdexcept>
#include <vector>

namespace hmmob {

// Ring buffer of interleaved float32 PCM. Frames are [L,R,L,R,...] for stereo.
// Write drops the oldest frames when full; Read returns the number of frames
// actually copied and leaves the rest of the destination untouched.
//
// Throws std::invalid_argument if channels or capacity_frames is not positive.
// All members are protected by an internal mutex, so Write (producer thread)
// and Read (consumer thread) may run concurrently.
class PcmRingBuffer {
public:
    PcmRingBuffer(int channels, int capacity_frames)
        : channels_(ValidateChannels(channels)),
          capacity_(ValidateCapacity(capacity_frames)),
          data_(static_cast<size_t>(channels_) * static_cast<size_t>(capacity_), 0.0f) {}

    void Write(const float* interleaved, int frames) {
        if (!interleaved || frames <= 0 || channels_ <= 0 || capacity_ <= 0) {
            return;
        }
        std::lock_guard<std::mutex> lock(mutex_);
        for (int i = 0; i < frames; ++i) {
            if (size_ == capacity_) {
                read_ = (read_ + 1) % capacity_;
                --size_;
            }
            const int dst = write_;
            for (int c = 0; c < channels_; ++c) {
                data_[static_cast<size_t>(dst) * channels_ + c] =
                    interleaved[static_cast<size_t>(i) * channels_ + c];
            }
            write_ = (write_ + 1) % capacity_;
            ++size_;
        }
    }

    [[nodiscard]] int Read(float* interleaved, int frames) {
        if (!interleaved || frames <= 0 || channels_ <= 0 || capacity_ <= 0) {
            return 0;
        }
        std::lock_guard<std::mutex> lock(mutex_);
        const int count = frames < size_ ? frames : size_;
        for (int i = 0; i < count; ++i) {
            for (int c = 0; c < channels_; ++c) {
                interleaved[static_cast<size_t>(i) * channels_ + c] =
                    data_[static_cast<size_t>(read_) * channels_ + c];
            }
            read_ = (read_ + 1) % capacity_;
            --size_;
        }
        return count;
    }

    void Clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        read_ = write_ = size_ = 0;
    }

    int available_frames() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return size_;
    }

    int channels() const { return channels_; }
    int capacity_frames() const { return capacity_; }

private:
    static int ValidateChannels(int channels) {
        if (channels <= 0) {
            throw std::invalid_argument("PcmRingBuffer: channels must be positive");
        }
        return channels;
    }

    static int ValidateCapacity(int capacity_frames) {
        if (capacity_frames <= 0) {
            throw std::invalid_argument("PcmRingBuffer: capacity_frames must be positive");
        }
        return capacity_frames;
    }

    mutable std::mutex mutex_;
    int channels_ = 0;
    int capacity_ = 0;
    std::vector<float> data_;
    int read_ = 0;
    int write_ = 0;
    int size_ = 0;
};

}  // namespace hmmob
