# Hmmob EV Phase 1 Audio Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 让 `evspike` 同时录制系统声音和麦克风，混音后编码为 AAC-LC，并与视频一起写入 MP4，形成带声音的录屏文件。

**Architecture:** 在现有 `hmmob_core` 中新增音频层：WASAPI 采集线程只负责把 float32 PCM 写入无锁/互斥保护的环形缓冲；主录制循环按视频帧节奏从 `AudioMixer` 拉取固定采样数，交给 `AacEncoder` 编码，再通过扩展后的 `FfmpegMuxer` 与视频包交错写入。音频包在视频首包写出 MP4 header 前先缓冲，避免容器头未就绪时丢音频。

**Tech Stack:** C++20、WASAPI（mmdevapi/audioclient）、FFmpeg 9 libavcodec/libavformat/libswresample、Catch2、现有 `hmmob_core`/`evspike` 结构。

---

## Scope / Non-goals

本次只做：

- 系统声 Loopback 采集。
- 默认麦克风采集。
- 两路 float32/48kHz/立体声混音（音量、静音、软限幅）。
- AAC-LC 192kbps 编码。
- MP4 音轨 + 音视频交错。
- `evspike` CLI 开关：`--no-audio`、`--no-mic`、`--no-system-audio`、`--mic-volume`、`--system-volume`。

暂不做：

- 默认音频设备热切换（`IMMNotificationClient`）——Phase 1.5。
- 噪声抑制、AGC、回声消除。
- 多音轨分离（系统声/麦克风独立轨道）。
- 音频主时钟驱动的丢/补视频帧——先保证两条 PTS 从 0 开始、时长一致。

## 现有代码约束

- `hmmob_core` 已链接 `${FFMPEG_LIBRARIES}`，包含 `avcodec/avformat/swresample/swscale`。
- `FfmpegMuxer` 当前只有视频流，header 延迟到首个视频包；需要扩展音频流并缓冲 header 前的音频包。
- `evspike` 的主循环是固定 FPS：每帧 encode 一帧视频。音频按 `sample_rate / fps` 拉取。
- 所有新代码遵循现有 `[[nodiscard]]`、`ComPtr`、`HMMOB_HR_CHECK`、`[integration]` 标签和 `SKIP` 模式。

## 文件结构

```
src/core/audio/
├─ PcmRingBuffer.h          无锁/互斥 PCM 环形缓冲（float32 interleaved）
├─ AudioMixer.h/.cpp        两路 PCM 混音、增益、静音、软限幅
├─ AacEncoder.h/.cpp        FFmpeg AAC-LC 编码器
└─ WasapiCapture.h/.cpp     WASAPI Loopback / 麦克风采集 + 格式转换

src/core/mux/FfmpegMuxer.h/.cpp   扩展音频流、WriteAudioPacket、header 前缓冲
src/spike/main.cpp                音频初始化、按帧拉取/编码/写包、CLI
tests/test_pcm_ring_buffer.cpp
tests/test_audio_mixer.cpp
tests/test_aac_encoder.cpp
tests/test_wasapi_capture.cpp
tests/test_ffmpeg_muxer.cpp       增加音视频交错测试
CMakeLists.txt
```

---

## Task 1: PCM 环形缓冲与混音器（TDD）

**Files:**
- Create: `src/core/audio/PcmRingBuffer.h`
- Create: `src/core/audio/AudioMixer.h`
- Create: `src/core/audio/AudioMixer.cpp`
- Create: `tests/test_pcm_ring_buffer.cpp`
- Create: `tests/test_audio_mixer.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: 写失败测试 `tests/test_pcm_ring_buffer.cpp`**

```cpp
#include <catch2/catch_test_macros.hpp>
#include <vector>
#include "core/audio/PcmRingBuffer.h"

TEST_CASE("PcmRingBuffer writes and reads interleaved stereo") {
    hmmob::PcmRingBuffer ring(2, 8);
    const float input[] = {1, 2, 3, 4, 5, 6};
    ring.Write(input, 3);
    REQUIRE(ring.available_frames() == 3);

    float output[6] = {};
    REQUIRE(ring.Read(output, 3) == 3);
    REQUIRE(output[0] == 1);
    REQUIRE(output[5] == 6);
    REQUIRE(ring.available_frames() == 0);
}

TEST_CASE("PcmRingBuffer drops oldest frames on overflow") {
    hmmob::PcmRingBuffer ring(1, 4);
    const float first[] = {1, 2, 3, 4};
    ring.Write(first, 4);
    const float extra[] = {5, 6};
    ring.Write(extra, 2);

    float output[4] = {};
    REQUIRE(ring.Read(output, 4) == 4);
    REQUIRE(output[0] == 3);
    REQUIRE(output[3] == 6);
}

TEST_CASE("PcmRingBuffer underflow returns available frames") {
    hmmob::PcmRingBuffer ring(1, 8);
    const float input[] = {1, 2};
    ring.Write(input, 2);

    float output[4] = {};
    REQUIRE(ring.Read(output, 4) == 2);
    REQUIRE(output[0] == 1);
    REQUIRE(output[1] == 2);
    REQUIRE(output[2] == 0);
}
```

- [ ] **Step 2: 写失败测试 `tests/test_audio_mixer.cpp`**

```cpp
#include <catch2/catch_test_macros.hpp>
#include <stdexcept>
#include "core/audio/AudioMixer.h"
#include "core/audio/PcmRingBuffer.h"

TEST_CASE("AudioMixer sums two sources with gains") {
    hmmob::PcmRingBuffer system(2, 16);
    hmmob::PcmRingBuffer mic(2, 16);
    const float a[] = {0.2f, 0.2f, 0.2f, 0.2f};
    const float b[] = {0.1f, 0.1f, 0.1f, 0.1f};
    system.Write(a, 2);
    mic.Write(b, 2);

    hmmob::AudioMixer mixer(48000, 2);
    mixer.SetSystemSource(&system);
    mixer.SetMicSource(&mic);
    mixer.SetSystemGain(1.0f);
    mixer.SetMicGain(2.0f);

    float out[4] = {};
    mixer.Mix(out, 2);
    // 原始和 0.2 + 2 * 0.1 = 0.4，经 tanh 软限幅后约为 0.37995。
    REQUIRE(out[0] > 0.379f);
    REQUIRE(out[0] < 0.381f);
    REQUIRE(out[2] > 0.379f);
    REQUIRE(out[2] < 0.381f);
}

TEST_CASE("AudioMixer soft-limits large input without hard clipping") {
    hmmob::PcmRingBuffer system(1, 8);
    const float loud[] = {9.0f, -9.0f};
    system.Write(loud, 2);

    hmmob::AudioMixer mixer(48000, 1);
    mixer.SetSystemSource(&system);
    float out[2] = {};
    mixer.Mix(out, 2);
    REQUIRE(out[0] > 0.99f);
    REQUIRE(out[0] < 1.0f);
    REQUIRE(out[1] < -0.99f);
    REQUIRE(out[1] > -1.0f);
}

TEST_CASE("AudioMixer treats missing sources as silence") {
    hmmob::AudioMixer mixer(48000, 2);
    float out[4] = {1, 1, 1, 1};
    mixer.Mix(out, 2);
    REQUIRE(out[0] == 0.0f);
    REQUIRE(out[3] == 0.0f);
}
```

- [ ] **Step 3: 修改 `CMakeLists.txt`**

在 `hmmob_core` 源文件中加入 `src/core/audio/AudioMixer.cpp`，在 `hmmob_tests` 中加入两个测试文件。无需新链接库。

- [ ] **Step 4: 构建确认失败**

```powershell
$cmake = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
& $cmake --preset windows-x64-release
& $cmake --build --preset windows-x64-release
```

预期：找不到 `core/audio/PcmRingBuffer.h` / `AudioMixer.h`。

- [ ] **Step 5: 实现 `src/core/audio/PcmRingBuffer.h`**

```cpp
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
// 构造时 channels / capacity_frames 必须为正，否则抛出 std::invalid_argument。
// 内部互斥锁保护，Write（采集线程）与 Read（录制线程）可并发。
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
```

- [ ] **Step 6: 实现 `src/core/audio/AudioMixer.h`**

```cpp
#pragma once
#include <cstddef>
#include <vector>

#include "core/audio/PcmRingBuffer.h"

namespace hmmob {

// 两路 float32 PCM 求和后经 tanh 软限幅。
//
// 非线程安全：设置与 Mix 应在同一录制线程调用；增益/静音在开始录制前配置。
// source 为非拥有指针，生命周期必须长于 mixer。source 声道数与 mixer 不一致时
// 直接忽略，避免按错误的声道数写入 scratch 造成越界。
//
// sample_rate / channels 非正时抛出 std::invalid_argument。
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

    // 预留：后续重采样 / 时间戳记账使用，当前未读取。
    int sample_rate_ = 48000;
    int channels_ = 2;
    PcmRingBuffer* system_ = nullptr;
    PcmRingBuffer* mic_ = nullptr;
    float system_gain_ = 1.0f;
    float mic_gain_ = 1.0f;
    bool system_muted_ = false;
    bool mic_muted_ = false;
    // 复用避免每次 Mix 堆分配。
    std::vector<float> system_scratch_;
    std::vector<float> mic_scratch_;
};

}  // namespace hmmob
```

- [ ] **Step 7: 实现 `src/core/audio/AudioMixer.cpp`**

```cpp
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
        // 声道数不一致时直接忽略，避免按 source 声道数写入 mixer 大小的 scratch。
        return;
    }
    const size_t samples = static_cast<size_t>(frames) * static_cast<size_t>(channels_);
    scratch.assign(samples, 0.0f);
    const int read = source->Read(scratch.data(), frames);
    if (muted) {
        // 已读取排空，保证生产端不会阻塞，但不贡献采样。
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

    // tanh 软限幅：结果严格落在 (-1, 1) 内，避免硬削波的失真。
    for (size_t i = 0; i < samples; ++i) {
        out[i] = std::tanh(out[i]);
    }
}

}  // namespace hmmob
```

- [ ] **Step 8: 构建并运行测试**

```powershell
& $cmake --build --preset windows-x64-release
& "...\ctest.exe" --test-dir build/release -C Release --output-on-failure -R "PcmRingBuffer|AudioMixer"
```

预期：聚焦的 PcmRingBuffer / AudioMixer 测试全部通过。

- [ ] **Step 9: 提交**

```powershell
git add src/core/audio tests/test_pcm_ring_buffer.cpp tests/test_audio_mixer.cpp CMakeLists.txt
git commit -m "feat: add pcm ring buffer and audio mixer"
```

---

## Task 2: AAC-LC 编码器

**Files:**
- Create: `src/core/audio/AacEncoder.h`
- Create: `src/core/audio/AacEncoder.cpp`
- Create: `tests/test_aac_encoder.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: 写失败测试 `tests/test_aac_encoder.cpp`**

```cpp
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <vector>
#include "core/audio/AacEncoder.h"

TEST_CASE("AacEncoder encodes 1 second of stereo sine") {
    constexpr int kSampleRate = 48000;
    constexpr int kChannels = 2;
    hmmob::AacEncoder encoder;
    REQUIRE(encoder.Initialize(kSampleRate, kChannels, 192000));

    std::vector<float> pcm(kSampleRate * kChannels);
    for (int i = 0; i < kSampleRate; ++i) {
        const float s = std::sin(2.0f * 3.14159265f * 440.0f * i / kSampleRate);
        pcm[i * 2 + 0] = s;
        pcm[i * 2 + 1] = s;
    }

    int packets = 0;
    size_t bytes = 0;
    REQUIRE(encoder.Encode(pcm.data(), kSampleRate, [&](const hmmob::AacPacket& p) {
        ++packets;
        bytes += p.data.size();
    }));
    REQUIRE(encoder.Flush([&](const hmmob::AacPacket& p) {
        ++packets;
        bytes += p.data.size();
    }));
    REQUIRE(packets > 0);
    REQUIRE(bytes > 0);
}
```

- [ ] **Step 2: 修改 `CMakeLists.txt`**

把 `src/core/audio/AacEncoder.cpp` 加入 `hmmob_core`，`tests/test_aac_encoder.cpp` 加入 `hmmob_tests`。

- [ ] **Step 3: 构建确认失败**

预期：找不到 `core/audio/AacEncoder.h`。

- [ ] **Step 4: 实现 `src/core/audio/AacEncoder.h`**

```cpp
#pragma once
#include <cstdint>
#include <functional>
#include <vector>

namespace hmmob {

struct AacPacket {
    std::vector<uint8_t> data;
    // PTS in samples under time_base = 1/sample_rate. The first packet is
    // negative by initial_padding_samples() (encoder priming).
    int64_t pts_samples = 0;
    // Decoded samples this packet produces (normally 1024; the final packet
    // may be shorter when AV_CODEC_CAP_SMALL_LAST_FRAME is supported).
    int duration_samples = 0;
};

// Stateful, not thread-safe. Encode accepts arbitrary chunk sizes; Flush emits
// the delayed tail; Encode/Flush fail after Flush until Initialize is called.
class AacEncoder {
public:
    using PacketCallback = std::function<void(const AacPacket&)>;

    AacEncoder() = default;
    ~AacEncoder();
    AacEncoder(const AacEncoder&) = delete;
    AacEncoder& operator=(const AacEncoder&) = delete;

    [[nodiscard]] bool Initialize(int sample_rate, int channels, int bitrate);
    [[nodiscard]] bool Encode(const float* interleaved, int frames,
                              const PacketCallback& callback);
    [[nodiscard]] bool Flush(const PacketCallback& callback);
    void Shutdown();
    [[nodiscard]] bool is_initialized() const;
    // Encoder delay in samples (1024 with FFmpeg's native AAC); 0 if unset.
    [[nodiscard]] int initial_padding_samples() const;
    // AudioSpecificConfig from the AAC encoder; must be copied into the MP4
    // audio codecpar before avformat_write_header.
    [[nodiscard]] const std::vector<uint8_t>& extradata() const;

private:
    struct Impl;
    Impl* impl_ = nullptr;
};

}  // namespace hmmob
```

- [ ] **Step 5: 实现 `src/core/audio/AacEncoder.cpp`**

```cpp
#include "core/audio/AacEncoder.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
#include <libavutil/samplefmt.h>
#include <libswresample/swresample.h>
}

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace hmmob {
namespace {
void LogAv(const char* what, int error) {
    std::fprintf(stderr, "AacEncoder: %s failed (%d)\n", what, error);
}
}  // namespace

struct AacEncoder::Impl {
    AVCodecContext* codec = nullptr;
    AVFrame* frame = nullptr;
    AVPacket* packet = nullptr;
    SwrContext* swr = nullptr;
    std::vector<float> pending;
    int sample_rate = 0;
    int channels = 0;
    int64_t next_pts_samples = 0;
    std::vector<uint8_t> extradata;

    bool Drain(const PacketCallback& callback) {
        for (;;) {
            const int result = avcodec_receive_packet(codec, packet);
            if (result == AVERROR(EAGAIN) || result == AVERROR_EOF) {
                return true;
            }
            if (result < 0) {
                LogAv("avcodec_receive_packet", result);
                return false;
            }
            AacPacket out;
            out.pts_samples = packet->pts;
            out.duration_samples = static_cast<int>(packet->duration);
            out.data.assign(packet->data, packet->data + packet->size);
            callback(out);
            av_packet_unref(packet);
        }
    }

    bool EncodeFrame(const PacketCallback& callback) {
        av_frame_unref(frame);
        frame->nb_samples = codec->frame_size;
        frame->format = codec->sample_fmt;
        frame->sample_rate = codec->sample_rate;
        av_channel_layout_copy(&frame->ch_layout, &codec->ch_layout);
        frame->pts = next_pts_samples;
        const int alloc = av_frame_get_buffer(frame, 0);
        if (alloc < 0) {
            LogAv("av_frame_get_buffer", alloc);
            return false;
        }
        const uint8_t* input[1] = {
            reinterpret_cast<const uint8_t*>(pending.data())};
        const int converted = swr_convert(
            swr, frame->data, frame->nb_samples,
            input, static_cast<int>(pending.size() / channels));
        if (converted < 0) {
            LogAv("swr_convert", converted);
            return false;
        }
        frame->nb_samples = converted;
        const int sent = avcodec_send_frame(codec, frame);
        if (sent < 0) {
            LogAv("avcodec_send_frame", sent);
            return false;
        }
        next_pts_samples += converted;
        pending.erase(pending.begin(), pending.begin() + static_cast<std::ptrdiff_t>(converted) * channels);
        return Drain(callback);
    }
};

AacEncoder::~AacEncoder() { Shutdown(); }

bool AacEncoder::Initialize(int sample_rate, int channels, int bitrate) {
    Shutdown();
    if (sample_rate <= 0 || channels <= 0 || bitrate <= 0) {
        return false;
    }
    impl_ = new Impl();
    impl_->sample_rate = sample_rate;
    impl_->channels = channels;

    const AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_AAC);
    if (!codec) {
        std::fprintf(stderr, "AacEncoder: no AAC encoder\n");
        Shutdown();
        return false;
    }
    impl_->codec = avcodec_alloc_context3(codec);
    if (!impl_->codec) {
        Shutdown();
        return false;
    }
    impl_->codec->sample_rate = sample_rate;
    impl_->codec->bit_rate = bitrate;
    impl_->codec->sample_fmt = AV_SAMPLE_FMT_FLTP;
    av_channel_layout_default(&impl_->codec->ch_layout, channels);
    impl_->codec->time_base = AVRational{1, sample_rate};
    const int open = avcodec_open2(impl_->codec, codec, nullptr);
    if (open < 0) {
        LogAv("avcodec_open2", open);
        Shutdown();
        return false;
    }
    if (impl_->codec->extradata && impl_->codec->extradata_size > 0) {
        impl_->extradata.assign(
            impl_->codec->extradata,
            impl_->codec->extradata + impl_->codec->extradata_size);
    }

    impl_->frame = av_frame_alloc();
    impl_->packet = av_packet_alloc();
    if (!impl_->frame || !impl_->packet) {
        Shutdown();
        return false;
    }

    AVChannelLayout in_layout;
    AVChannelLayout out_layout;
    av_channel_layout_default(&in_layout, channels);
    av_channel_layout_default(&out_layout, channels);
    const int swr_result = swr_alloc_set_opts2(
        &impl_->swr, &out_layout, AV_SAMPLE_FMT_FLTP, sample_rate,
        &in_layout, AV_SAMPLE_FMT_FLT, sample_rate, 0, nullptr);
    if (swr_result < 0 || swr_init(impl_->swr) < 0) {
        LogAv("swr_init", swr_result);
        Shutdown();
        return false;
    }
    return true;
}

bool AacEncoder::Encode(const float* interleaved, int frames,
                        const PacketCallback& callback) {
    if (!impl_ || !impl_->codec || !interleaved || frames <= 0) {
        return false;
    }
    impl_->pending.insert(impl_->pending.end(), interleaved,
                          interleaved + static_cast<size_t>(frames) * impl_->channels);
    while (static_cast<int>(impl_->pending.size() / impl_->channels) >=
           impl_->codec->frame_size) {
        if (!impl_->EncodeFrame(callback)) {
            return false;
        }
    }
    return true;
}

bool AacEncoder::Flush(const PacketCallback& callback) {
    if (!impl_ || !impl_->codec) {
        return false;
    }
    if (!impl_->pending.empty()) {
        const int need = impl_->codec->frame_size * impl_->channels;
        impl_->pending.resize(static_cast<size_t>(need), 0.0f);
        if (!impl_->EncodeFrame(callback)) {
            return false;
        }
    }
    const int sent = avcodec_send_frame(impl_->codec, nullptr);
    if (sent < 0 && sent != AVERROR_EOF) {
        LogAv("avcodec_send_frame(flush)", sent);
        return false;
    }
    return impl_->Drain(callback);
}

void AacEncoder::Shutdown() {
    if (!impl_) {
        return;
    }
    if (impl_->swr) {
        swr_free(&impl_->swr);
    }
    if (impl_->packet) {
        av_packet_free(&impl_->packet);
    }
    if (impl_->frame) {
        av_frame_free(&impl_->frame);
    }
    if (impl_->codec) {
        avcodec_free_context(&impl_->codec);
    }
    delete impl_;
    impl_ = nullptr;
}

bool AacEncoder::is_initialized() const {
    return impl_ && impl_->codec;
}

const std::vector<uint8_t>& AacEncoder::extradata() const {
    static const std::vector<uint8_t> empty;
    return impl_ ? impl_->extradata : empty;
}

}  // namespace hmmob
```

- [ ] **Step 6: 构建并运行 AAC 测试**

```powershell
& $cmake --build --preset windows-x64-release
& "...\ctest.exe" --test-dir build/release -C Release --output-on-failure -R AacEncoder
```

预期：通过，packets/bytes > 0。

- [ ] **Step 7: 提交**

```powershell
git add src/core/audio/AacEncoder.* tests/test_aac_encoder.cpp CMakeLists.txt
git commit -m "feat: add aac-lc encoder"
```

---

## Task 3: 给 FfmpegMuxer 增加音频流

**Files:**
- Modify: `src/core/mux/FfmpegMuxer.h`
- Modify: `src/core/mux/FfmpegMuxer.cpp`
- Modify: `tests/test_ffmpeg_muxer.cpp`
- Modify: `src/spike/main.cpp`（仅适配 Open 新参数，本任务音频关闭）

- [ ] **Step 1: 扩展 `FfmpegMuxer.h`**

先在 `FfmpegMuxer.h` 顶部加入 `#include <vector>`，然后在 `namespace hmmob` 中新增：

```cpp
struct AudioConfig {
    bool enabled = false;
    int sample_rate = 48000;
    int channels = 2;
    int bitrate = 192000;
    // Encoder priming delay in samples (AacEncoder::initial_padding_samples).
    // Metadata only for MP4 in FFmpeg 9: movenc derives the edit list from the
    // negative first DTS, so the negative first PTS must NOT be clamped.
    int initial_padding = 0;
    // AudioSpecificConfig from the AAC encoder.
    std::vector<uint8_t> extradata;
};
```

把 `Open` 改为：

```cpp
bool Open(const std::string& path, const std::string& codec,
          int width, int height, int fps, int bitrate,
          const AudioConfig& audio = {});
```

新增：

```cpp
// Writes one AAC access unit. If the container header has not been written
// yet (waiting for the first video packet), the packet is buffered and
// flushed when the header is written. `duration_samples` comes from
// AacPacket::duration_samples (stream time_base is 1/sample_rate).
bool WriteAudioPacket(const uint8_t* data, size_t size, int64_t pts_us,
                      int duration_samples);
```

在类注释里补充：音频包在 header 前最多缓冲 5 秒；超过则失败。

- [ ] **Step 2: 修改 `FfmpegMuxer.cpp`**

在 `Impl` 中增加：

```cpp
AVStream* audio_stream = nullptr;
AudioConfig audio;
struct PendingAudio {
    std::vector<uint8_t> data;
    int64_t pts_us;
    int duration_samples;
};
std::vector<PendingAudio> pending_audio;
```

在 `Open` 中创建音频流（视频流创建之后、`avio_open` 之前）：

```cpp
if (audio.enabled) {
    impl_->audio = audio;
    impl_->audio_stream = avformat_new_stream(impl_->format, nullptr);
    if (!impl_->audio_stream) {
        Close();
        return false;
    }
    AVCodecParameters* par = impl_->audio_stream->codecpar;
    par->codec_type = AVMEDIA_TYPE_AUDIO;
    par->codec_id = AV_CODEC_ID_AAC;
    par->sample_rate = audio.sample_rate;
    av_channel_layout_default(&par->ch_layout, audio.channels);
    // Encoded AAC is not PCM; leave the sample format unspecified.
    par->format = AV_SAMPLE_FMT_NONE;
    par->bit_rate = audio.bitrate;
    par->codec_tag = 0;
    par->frame_size = 1024;
    // Priming delay; movenc turns this into the MP4 edit list.
    par->initial_padding = audio.initial_padding;
    if (!audio.extradata.empty()) {
        par->extradata = static_cast<uint8_t*>(av_mallocz(
            audio.extradata.size() + AV_INPUT_BUFFER_PADDING_SIZE));
        if (!par->extradata) {
            Close();
            return false;
        }
        std::memcpy(par->extradata, audio.extradata.data(),
                    audio.extradata.size());
        par->extradata_size = static_cast<int>(audio.extradata.size());
    }
    impl_->audio_stream->time_base = AVRational{1, audio.sample_rate};
}
```

不要在写入前把音频首包的负 PTS（`-initial_padding`）截断为 0：movenc 会依据
`codecpar->initial_padding` 写 edit list，把 priming 从成片时间线上去掉。

在 `WritePacket` 的视频首包 header 写出之后（`impl_->header_written = true;` 之后）加入：

```cpp
for (const auto& pending : impl_->pending_audio) {
    if (!WriteAudioPacket(pending.data.data(), pending.data.size(),
                          pending.pts_us, pending.duration_samples)) {
        return false;
    }
}
impl_->pending_audio.clear();
```

实现 `WriteAudioPacket`：

```cpp
bool FfmpegMuxer::WriteAudioPacket(const uint8_t* data, size_t size, int64_t pts_us,
                                   int duration_samples) {
    if (!impl_ || !impl_->format || !impl_->audio_stream || !data || size == 0 ||
        impl_->write_failed) {
        return false;
    }
    if (!impl_->header_written) {
        const int64_t max_pending_us = 5'000'000;
        if (!impl_->pending_audio.empty() &&
            pts_us - impl_->pending_audio.front().pts_us > max_pending_us) {
            std::fprintf(stderr, "FfmpegMuxer: audio buffer overflow before video\n");
            impl_->write_failed = true;
            return false;
        }
        impl_->pending_audio.push_back(
            {std::vector<uint8_t>(data, data + size), pts_us, duration_samples});
        return true;
    }

    AVPacket* packet = av_packet_alloc();
    if (!packet) {
        return false;
    }
    if (av_new_packet(packet, static_cast<int>(size)) < 0) {
        av_packet_free(&packet);
        return false;
    }
    std::memcpy(packet->data, data, size);
    packet->pts = pts_us;
    packet->dts = pts_us;
    packet->stream_index = impl_->audio_stream->index;
    av_packet_rescale_ts(packet, kMicrosecondTimeBase, impl_->audio_stream->time_base);
    // After rescaling the stream time base is 1/sample_rate, so the duration
    // is already expressed in samples.
    packet->duration = duration_samples;
    const int result = av_interleaved_write_frame(impl_->format, packet);
    av_packet_free(&packet);
    if (result < 0) {
        LogAvError("av_interleaved_write_frame(audio)", result);
        impl_->write_failed = true;
        return false;
    }
    return true;
}
```

在 `Close` 的 `header_written` 分支之前，若 `pending_audio` 非空且从未写 header，则视为失败：

```cpp
if (!impl_->pending_audio.empty() && !impl_->header_written) {
    ok = false;
}
```

- [ ] **Step 3: 适配 `src/spike/main.cpp`**

`muxer.Open(...)` 调用保持原样即可（默认 `AudioConfig{}` 关闭音频）。确认编译通过。

- [ ] **Step 4: 扩展 `tests/test_ffmpeg_muxer.cpp`**

新增一个 `[integration][amf]` 测试：用 AMF 编码 30 帧 640×360 视频，同时用 `AacEncoder` 编码 1 秒 48kHz 立体声正弦，`Open` 时启用音频，交替 `WritePacket`/`WriteAudioPacket`，`Close` 后用 `avformat` 打开，断言存在 HEVC 视频流和 AAC 音频流，音频 `sample_rate==48000`、`ch_layout.nb_channels==2`，并至少解码一个音频帧。测试完成后删除临时文件。

- [ ] **Step 5: 构建并运行**

```powershell
& $cmake --build --preset windows-x64-release
& "...\ctest.exe" --test-dir build/release -C Release --output-on-failure -R "FFmpeg muxer"
```

预期：原 HEVC/H.264 测试与新 A/V 测试全部通过。

- [ ] **Step 6: 提交**

```powershell
git add src/core/mux/FfmpegMuxer.* tests/test_ffmpeg_muxer.cpp src/spike/main.cpp
git commit -m "feat: add aac audio stream to mp4 muxer"
```

---

## Task 4: WASAPI 系统声与麦克风采集

**Files:**
- Create: `src/core/audio/WasapiCapture.h`
- Create: `src/core/audio/WasapiCapture.cpp`
- Create: `tests/test_wasapi_capture.cpp`
- Modify: `CMakeLists.txt`（链接 `mmdevapi ole32 avrt propsys`）

- [ ] **Step 1: 写集成测试 `tests/test_wasapi_capture.cpp`**

```cpp
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <thread>
#include <vector>
#include "core/audio/WasapiCapture.h"

TEST_CASE("WasapiCapture loopback receives packets", "[integration][audio]") {
    hmmob::WasapiCapture capture;
    if (!capture.Initialize(true, 48000, 2)) {
        SKIP("no default render endpoint");
    }
    REQUIRE(capture.Start());
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    std::vector<float> buffer(48000 * 2);
    const int frames = capture.Read(buffer.data(), 4800);
    capture.Stop();
    REQUIRE(frames >= 0);
    REQUIRE(capture.ring() != nullptr);
}

TEST_CASE("WasapiCapture microphone receives packets", "[integration][audio]") {
    hmmob::WasapiCapture capture;
    if (!capture.Initialize(false, 48000, 2)) {
        SKIP("no default capture endpoint");
    }
    REQUIRE(capture.Start());
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    std::vector<float> buffer(48000 * 2);
    const int frames = capture.Read(buffer.data(), 4800);
    capture.Stop();
    REQUIRE(frames >= 0);
}
```

- [ ] **Step 2: 修改 `CMakeLists.txt`**

加入 `src/core/audio/WasapiCapture.cpp`，并给 `hmmob_core` PUBLIC 链接：

```cmake
target_link_libraries(hmmob_core PUBLIC
  d3d11 dxgi d3dcompiler mmdevapi ole32 avrt propsys ${FFMPEG_LIBRARIES})
```

加入测试源 `tests/test_wasapi_capture.cpp`。

- [ ] **Step 3: 构建确认失败**

预期：找不到 `core/audio/WasapiCapture.h`。

- [ ] **Step 4: 实现 `src/core/audio/WasapiCapture.h`**

```cpp
#pragma once
#include "core/audio/PcmRingBuffer.h"
#include <cstdint>
#include <memory>

namespace hmmob {

class WasapiCapture {
public:
    WasapiCapture() = default;
    ~WasapiCapture();
    WasapiCapture(const WasapiCapture&) = delete;
    WasapiCapture& operator=(const WasapiCapture&) = delete;

    // loopback=true captures the default render endpoint (system sound);
    // loopback=false captures the default microphone.
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
```

- [ ] **Step 5: 实现 `src/core/audio/WasapiCapture.cpp`**

```cpp
#include "core/audio/WasapiCapture.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <audioclient.h>
#include <mmdeviceapi.h>
#include <mmreg.h>
#include <ksmedia.h>
#include <functiondiscoverykeys_devpkey.h>
#include <propsys.h>
#include <wrl/client.h>

extern "C" {
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
#include <libavutil/samplefmt.h>
#include <libswresample/swresample.h>
}

#include <atomic>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>

namespace hmmob {
namespace {
using Microsoft::WRL::ComPtr;

void LogHr(const char* what, HRESULT hr) {
    std::fprintf(stderr, "WasapiCapture: %s failed 0x%08lx\n", what,
                 static_cast<unsigned long>(hr));
}
}  // namespace

struct WasapiCapture::Impl {
    bool loopback = false;
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

    bool ConvertAndPush(const BYTE* data, UINT32 frames) {
        if (!data || frames == 0) {
            return true;
        }
        if (!swr) {
            ring->Write(reinterpret_cast<const float*>(data), static_cast<int>(frames));
            return true;
        }
        const int capacity = swr_get_out_samples(swr, static_cast<int>(frames));
        std::vector<float> out(static_cast<size_t>(capacity) * channels, 0.0f);
        const uint8_t* input[1] = {data};
        const int converted = swr_convert(
            swr, reinterpret_cast<uint8_t**>(out.data()), capacity,
            input, static_cast<int>(frames));
        if (converted < 0) {
            return false;
        }
        ring->Write(out.data(), converted);
        return true;
    }

    PcmRingBuffer* ring = nullptr;
};

WasapiCapture::~WasapiCapture() { Shutdown(); }

bool WasapiCapture::Initialize(bool loopback, int sample_rate, int channels) {
    Shutdown();
    if (sample_rate <= 0 || channels <= 0) {
        return false;
    }
    sample_rate_ = sample_rate;
    channels_ = channels;
    ring_ = std::make_unique<PcmRingBuffer>(channels, sample_rate);

    impl_ = std::make_unique<Impl>();
    impl_->loopback = loopback;
    impl_->sample_rate = sample_rate;
    impl_->channels = channels;
    impl_->ring = ring_.get();

    const HRESULT co = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(co) && co != RPC_E_CHANGED_MODE) {
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

    DWORD flags = AUDCLNT_STREAMFLAGS_EVENTCALLBACK;
    if (loopback) {
        flags |= AUDCLNT_STREAMFLAGS_LOOPBACK;
    }
    hr = impl_->client->Initialize(AUDCLNT_SHAREMODE_SHARED, flags, 0, 0,
                                   impl_->mix_format, nullptr);
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
    impl_->event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!impl_->event) {
        Shutdown();
        return false;
    }
    hr = impl_->client->SetEventHandle(impl_->event);
    if (FAILED(hr)) {
        LogHr("SetEventHandle", hr);
        Shutdown();
        return false;
    }

    const bool plain_float =
        impl_->mix_format->wFormatTag == WAVE_FORMAT_IEEE_FLOAT;
    const bool extensible_float =
        impl_->mix_format->wFormatTag == WAVE_FORMAT_EXTENSIBLE &&
        IsEqualGUID(impl_->mix_format->SubFormat,
                    KSDATAFORMAT_SUBTYPE_IEEE_FLOAT);
    if (!plain_float && !extensible_float) {
        std::fprintf(stderr,
                     "WasapiCapture: mix format is not 32-bit float; "
                     "unsupported in Phase 1\n");
        Shutdown();
        return false;
    }

    const bool same_format =
        impl_->mix_format->nSamplesPerSec == static_cast<DWORD>(sample_rate) &&
        impl_->mix_format->nChannels == static_cast<WORD>(channels) &&
        impl_->mix_format->wBitsPerSample == 32;
    if (!same_format) {
        AVChannelLayout in_layout;
        AVChannelLayout out_layout;
        av_channel_layout_default(&in_layout, impl_->mix_format->nChannels);
        av_channel_layout_default(&out_layout, channels);
        const int result = swr_alloc_set_opts2(
            &impl_->swr, &out_layout, AV_SAMPLE_FMT_FLT, sample_rate,
            &in_layout, AV_SAMPLE_FMT_FLT, impl_->mix_format->nSamplesPerSec,
            0, nullptr);
        if (result < 0 || swr_init(impl_->swr) < 0) {
            std::fprintf(stderr, "WasapiCapture: unsupported mix format\n");
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
    impl_->running = true;
    impl_->thread = std::thread([this] {
        while (impl_->running) {
            WaitForSingleObject(impl_->event, 100);
            UINT32 packet_frames = 0;
            while (impl_->running &&
                   SUCCEEDED(impl_->capture->GetNextPacketSize(&packet_frames)) &&
                   packet_frames > 0) {
                BYTE* data = nullptr;
                UINT32 frames = 0;
                DWORD flags = 0;
                if (FAILED(impl_->capture->GetBuffer(&data, &frames, &flags,
                                                     nullptr, nullptr))) {
                    break;
                }
                if (flags & AUDCLNT_BUFFERFLAGS_SILENT) {
                    std::vector<float> zeros(
                        static_cast<size_t>(frames) * impl_->channels, 0.0f);
                    impl_->ring->Write(zeros.data(), static_cast<int>(frames));
                } else {
                    impl_->ConvertAndPush(data, frames);
                }
                impl_->capture->ReleaseBuffer(frames);
            }
        }
    });
    return true;
}

void WasapiCapture::Stop() {
    if (!impl_ || !running_) {
        return;
    }
    running_ = false;
    impl_->running = false;
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
        return;
    }
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
    impl_->capture.Reset();
    impl_->client.Reset();
    impl_->device.Reset();
    impl_->enumerator.Reset();
    impl_.reset();
    ring_.reset();
    CoUninitialize();
}

}  // namespace hmmob
```

- [ ] **Step 6: 构建并运行 WASAPI 测试**

```powershell
& $cmake --build --preset windows-x64-release
& "...\ctest.exe" --test-dir build/release -C Release --output-on-failure -R WasapiCapture
```

预期：有默认设备时通过并收到数据；无设备时 SKIP。

- [ ] **Step 7: 提交**

```powershell
git add src/core/audio/WasapiCapture.* tests/test_wasapi_capture.cpp CMakeLists.txt
git commit -m "feat: add wasapi loopback and microphone capture"
```

---

## Task 5: evspike 音频集成

**Files:**
- Modify: `src/spike/main.cpp`
- Modify: `README.md`

- [ ] **Step 1: 扩展 Options/Parse**

新增：

```cpp
bool audio = true;
bool system_audio = true;
bool microphone = true;
float system_volume = 1.0f;
float mic_volume = 1.0f;
```

解析：

```cpp
} else if (arg == "--no-audio") {
    options.audio = false;
} else if (arg == "--no-system-audio") {
    options.system_audio = false;
} else if (arg == "--no-mic") {
    options.microphone = false;
} else if (arg == "--system-volume") {
    const char* v = next("--system-volume"); if (!v) return false;
    options.system_volume = std::stof(v);
} else if (arg == "--mic-volume") {
    const char* v = next("--mic-volume"); if (!v) return false;
    options.mic_volume = std::stof(v);
}
```

`--help` 增加对应说明。

- [ ] **Step 2: 初始化音频**

在 `FfmpegMuxer muxer` 之前加入：

```cpp
constexpr int kAudioSampleRate = 48000;
constexpr int kAudioChannels = 2;
constexpr int kAudioBitrate = 192000;

hmmob::WasapiCapture system_capture;
hmmob::WasapiCapture mic_capture;
bool audio_enabled = options.audio;
bool system_ok = false;
bool mic_ok = false;

if (audio_enabled && options.system_audio) {
    system_ok = system_capture.Initialize(true, kAudioSampleRate, kAudioChannels);
    if (system_ok) {
        system_capture.Start();
    } else {
        std::fprintf(stderr, "warning: system audio unavailable\n");
    }
}
if (audio_enabled && options.microphone) {
    mic_ok = mic_capture.Initialize(false, kAudioSampleRate, kAudioChannels);
    if (mic_ok) {
        mic_capture.Start();
    } else {
        std::fprintf(stderr, "warning: microphone unavailable\n");
    }
}
audio_enabled = audio_enabled && (system_ok || mic_ok);

hmmob::AudioMixer audio_mixer(kAudioSampleRate, kAudioChannels);
audio_mixer.SetSystemSource(system_ok ? system_capture.ring() : nullptr);
audio_mixer.SetMicSource(mic_ok ? mic_capture.ring() : nullptr);
audio_mixer.SetSystemGain(options.system_volume);
audio_mixer.SetMicGain(options.mic_volume);

hmmob::AacEncoder aac;
if (audio_enabled && !aac.Initialize(kAudioSampleRate, kAudioChannels, kAudioBitrate)) {
    std::fprintf(stderr, "failed to initialize AAC encoder\n");
    return 1;
}
```

`FfmpegMuxer::Open` 传入：

```cpp
hmmob::AudioConfig audio_config;
audio_config.enabled = audio_enabled;
if (audio_enabled) {
    audio_config.extradata = aac.extradata();
    audio_config.initial_padding = aac.initial_padding_samples();
}
muxer.Open(..., audio_config);
```

- [ ] **Step 3: 音频包写入**

定义：

```cpp
int audio_packets = 0;
int64_t audio_bytes = 0;
int64_t audio_samples_written = 0;
const auto on_audio_packet = [&](const hmmob::AacPacket& packet) {
    const int64_t pts_us = packet.pts_samples * 1'000'000 / kAudioSampleRate;
    if (!muxer.WriteAudioPacket(packet.data.data(), packet.data.size(), pts_us,
                                packet.duration_samples)) {
        write_failed = true;
        return;
    }
    ++audio_packets;
    audio_bytes += static_cast<int64_t>(packet.data.size());
};
```

在主循环每帧视频编码之后加入：

```cpp
if (audio_enabled) {
    // Match audio sample count to the video frame timeline without drift,
    // even when fps does not divide 48000.
    const int64_t target_samples =
        static_cast<int64_t>(encoded_frames + 1) * kAudioSampleRate / options.fps;
    const int frames_to_mix =
        static_cast<int>(target_samples - audio_samples_written);
    if (frames_to_mix > 0) {
        std::vector<float> audio_buffer(
            static_cast<size_t>(frames_to_mix) * kAudioChannels);
        audio_mixer.Mix(audio_buffer.data(), frames_to_mix);
        if (!aac.Encode(audio_buffer.data(), frames_to_mix, on_audio_packet) ||
            write_failed) {
            std::fprintf(stderr, "audio encode/write failed\n");
            failed = true;
            break;
        }
        audio_samples_written += frames_to_mix;
    }
}
```

在 `encoder.Flush` 之前或之后（推荐在视频 flush 之前）加入：

```cpp
if (audio_enabled && !aac.Flush(on_audio_packet)) {
    std::fprintf(stderr, "AAC flush failed\n");
    failed = true;
}
```

停止采集：

```cpp
system_capture.Stop();
mic_capture.Stop();
```

打印 stats 增加：

```cpp
std::printf("audio_packets=%d audio_bytes=%lld\n", audio_packets,
            static_cast<long long>(audio_bytes));
```

- [ ] **Step 4: 构建并运行 3 秒带音频录制**

```powershell
& $cmake --build --preset windows-x64-release
.\build\release\Release\evspike.exe --output C:/temp/hmmob-audio.mp4 --seconds 3 --fps 60 --codec hevc --bitrate 50000000
& "C:\Users\Administrator\AppData\Local\JianyingPro\Apps\11.3.0.14362\ffmpeg.exe" -hide_banner -i C:/temp/hmmob-audio.mp4
& "C:\Users\Administrator\AppData\Local\JianyingPro\Apps\11.3.0.14362\ffmpeg.exe" -v error -i C:/temp/hmmob-audio.mp4 -f null -
```

预期：MP4 同时包含 HEVC 视频流和 AAC 音频流（48000Hz、stereo）；解码 exit 0；音频包数 > 0。

- [ ] **Step 5: 无音频/仅麦克风/仅系统声路径**

```powershell
.\build\release\Release\evspike.exe --output C:/temp/noaudio.mp4 --seconds 2 --no-audio
.\build\release\Release\evspike.exe --output C:/temp/mic.mp4 --seconds 2 --no-system-audio
.\build\release\Release\evspike.exe --output C:/temp/sys.mp4 --seconds 2 --no-mic
```

预期：`--no-audio` 只有视频流；另外两个至少有一条音轨或降级警告，不崩溃。

- [ ] **Step 6: 提交**

```powershell
git add src/spike/main.cpp README.md
git commit -m "feat: record system and microphone audio"
```

---

## Task 6: 文档与验收

**Files:**
- Modify: `docs/phase0-report.md`（或新建 `docs/phase1-audio-report.md`）
- Modify: `README.md`

- [ ] **Step 1: 记录 3 秒与 10 秒带音频结果**

写入 `docs/phase1-audio-report.md`：

- 环境与命令。
- `ffmpeg -i` 输出（HEVC + AAC、采样率、声道数）。
- 解码 exit code。
- 音频包数/字节数。
- 限制：设备热切换、噪声抑制、音频主时钟丢补帧未做；Loopback 在无声时是静音。

- [ ] **Step 2: 更新 README**

增加音频开关说明和带音频运行示例。

- [ ] **Step 3: 提交**

```powershell
git add docs/phase1-audio-report.md README.md
git commit -m "docs: add phase 1 audio report"
```

---

## Self-Review

- **Spec coverage**：系统声 Loopback、麦克风、48kHz float32、混音+软限幅、AAC-LC 192kbps、MP4 音轨、PTS 由采样数计算均有对应任务。设备热切换、噪声抑制、音频主时钟丢补帧明确列为非目标/Phase 1.5。
- **Placeholder scan**：无 TBD/TODO；每个代码步骤给出完整文件或精确 diff。
- **Type consistency**：`PcmRingBuffer`、`AudioMixer`、`AacEncoder`、`AacPacket`、`AudioConfig`、`WasapiCapture`、`FfmpegMuxer::WriteAudioPacket` 的命名和签名在任务间保持一致。
- **Integration risk**：`FfmpegMuxer` header 前音频缓冲、视频首包 extradata、AAC 编码器 flush、采集线程与主循环的环形缓冲边界都在任务中明确。

## Execution Handoff

Plan complete and saved to `docs/superpowers/plans/2026-11-07-phase1-audio.md`.

两种执行方式：

1. **Subagent-Driven（推荐）**：每个 Task 派一个全新子代理，任务之间做 review。
2. **Inline Execution**：在当前会话按批次执行。
