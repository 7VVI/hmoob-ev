#include "core/audio/AacEncoder.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/channel_layout.h>
#include <libavutil/error.h>
#include <libavutil/samplefmt.h>
#include <libswresample/swresample.h>
}

#include <algorithm>
#include <cstddef>
#include <cstdio>

namespace hmmob {
namespace {
void LogAv(const char* what, int error) {
    char message[AV_ERROR_MAX_STRING_SIZE] = {};
    av_strerror(error, message, sizeof(message));
    std::fprintf(stderr, "AacEncoder: %s failed: %s (%d)\n", what, message, error);
}
}  // namespace

struct AacEncoder::Impl {
    AVCodecContext* codec = nullptr;
    // Static codec descriptor (owned by libavcodec); its capabilities replace
    // AVCodecContext::capabilities, which FFmpeg 9 no longer exposes.
    const AVCodec* codec_def = nullptr;
    AVFrame* frame = nullptr;
    AVPacket* packet = nullptr;
    SwrContext* swr = nullptr;
    // Interleaved float32 samples not yet handed to the encoder.
    std::vector<float> pending;
    int sample_rate = 0;
    int channels = 0;
    int bitrate = 0;
    int64_t next_pts_samples = 0;
    bool flushed = false;
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

    bool SendFrame(const AVFrame* input, const PacketCallback& callback) {
        int sent = avcodec_send_frame(codec, input);
        if (sent == AVERROR(EAGAIN)) {
            // The encoder still holds output packets; drain and retry once.
            if (!Drain(callback)) {
                return false;
            }
            sent = avcodec_send_frame(codec, input);
        }
        if (sent < 0) {
            LogAv("avcodec_send_frame", sent);
            return false;
        }
        return Drain(callback);
    }

    // Encodes the first `samples_to_encode` frames of `pending`. Callers pass
    // frame_size for regular frames, or a shorter count for a small last frame.
    // The input is consumed via swr; pts advance by the converted sample count.
    bool EncodeFrame(int samples_to_encode, const PacketCallback& callback) {
        const int available = static_cast<int>(pending.size() / channels);
        if (samples_to_encode <= 0 || samples_to_encode > available ||
            samples_to_encode > codec->frame_size) {
            LogAv("EncodeFrame", AVERROR(EINVAL));
            return false;
        }
        av_frame_unref(frame);
        frame->nb_samples = samples_to_encode;
        frame->format = codec->sample_fmt;
        frame->sample_rate = codec->sample_rate;
        const int layout = av_channel_layout_copy(&frame->ch_layout, &codec->ch_layout);
        if (layout < 0) {
            LogAv("av_channel_layout_copy", layout);
            return false;
        }
        frame->pts = next_pts_samples;
        const int alloc = av_frame_get_buffer(frame, 0);
        if (alloc < 0) {
            LogAv("av_frame_get_buffer", alloc);
            return false;
        }
        const uint8_t* input[1] = {
            reinterpret_cast<const uint8_t*>(pending.data())};
        const int converted = swr_convert(
            swr, frame->data, frame->nb_samples, input, samples_to_encode);
        if (converted < 0) {
            LogAv("swr_convert", converted);
            return false;
        }
        // Only same-rate FLT -> FLTP conversion is supported here, so the
        // converter must be sample-exact. Rate conversion would need
        // swr_get_delay() and delayed-input PTS accounting instead.
        if (converted != samples_to_encode) {
            std::fprintf(stderr,
                         "AacEncoder: swr_convert produced %d of %d samples\n",
                         converted, samples_to_encode);
            return false;
        }
        if (!SendFrame(frame, callback)) {
            return false;
        }
        next_pts_samples += converted;
        pending.erase(
            pending.begin(),
            pending.begin() + static_cast<std::ptrdiff_t>(samples_to_encode) * channels);
        return true;
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
    impl_->bitrate = bitrate;

    const AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_AAC);
    if (!codec) {
        std::fprintf(stderr, "AacEncoder: no AAC encoder available\n");
        Shutdown();
        return false;
    }
    impl_->codec = avcodec_alloc_context3(codec);
    if (!impl_->codec) {
        Shutdown();
        return false;
    }
    impl_->codec_def = codec;
    impl_->codec->sample_rate = sample_rate;
    impl_->codec->bit_rate = bitrate;
    impl_->codec->sample_fmt = AV_SAMPLE_FMT_FLTP;
    impl_->codec->profile = AV_PROFILE_AAC_LOW;
    // Keep global-header mode for MP4 output. The native encoder emits a
    // 5-byte AudioSpecificConfig (base ASC plus the SBR-absent sync extension)
    // that must be copied verbatim into codecpar; empty extradata is fatal.
    impl_->codec->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    av_channel_layout_default(&impl_->codec->ch_layout, channels);
    impl_->codec->time_base = AVRational{1, sample_rate};
    const int open = avcodec_open2(impl_->codec, codec, nullptr);
    if (open < 0) {
        LogAv("avcodec_open2", open);
        Shutdown();
        return false;
    }
    if (!impl_->codec->extradata || impl_->codec->extradata_size <= 0) {
        std::fprintf(stderr, "AacEncoder: encoder produced no extradata\n");
        Shutdown();
        return false;
    }
    impl_->extradata.assign(impl_->codec->extradata,
                            impl_->codec->extradata + impl_->codec->extradata_size);

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
        &impl_->swr, &out_layout, AV_SAMPLE_FMT_FLTP, sample_rate, &in_layout,
        AV_SAMPLE_FMT_FLT, sample_rate, 0, nullptr);
    if (swr_result < 0 || swr_init(impl_->swr) < 0) {
        LogAv("swr_init", swr_result);
        Shutdown();
        return false;
    }
    if (impl_->codec->frame_size <= 0) {
        std::fprintf(stderr, "AacEncoder: encoder reported no frame size\n");
        Shutdown();
        return false;
    }
    return true;
}

bool AacEncoder::Encode(const float* interleaved, int frames,
                        const PacketCallback& callback) {
    if (!impl_ || !impl_->codec || impl_->flushed || !callback || frames < 0) {
        return false;
    }
    if (frames == 0) {
        // Nothing to encode; an empty input is a benign no-op.
        return true;
    }
    if (!interleaved) {
        return false;
    }
    impl_->pending.insert(
        impl_->pending.end(), interleaved,
        interleaved + static_cast<size_t>(frames) * static_cast<size_t>(impl_->channels));
    const int frame_size = impl_->codec->frame_size;
    while (static_cast<int>(impl_->pending.size() /
                            static_cast<size_t>(impl_->channels)) >= frame_size) {
        if (!impl_->EncodeFrame(frame_size, callback)) {
            return false;
        }
    }
    return true;
}

bool AacEncoder::Flush(const PacketCallback& callback) {
    if (!impl_ || !impl_->codec || impl_->flushed || !callback) {
        return false;
    }
    const int frame_size = impl_->codec->frame_size;
    const size_t max_samples =
        static_cast<size_t>(frame_size) * static_cast<size_t>(impl_->channels);
    if (impl_->pending.size() > max_samples) {
        // Encode() always drains full frames, so this indicates a logic error;
        // resizing would silently discard samples.
        std::fprintf(stderr, "AacEncoder: pending tail exceeds one frame\n");
        return false;
    }
    if (!impl_->pending.empty()) {
        const int remaining =
            static_cast<int>(impl_->pending.size() / static_cast<size_t>(impl_->channels));
        if (impl_->codec_def->capabilities & AV_CODEC_CAP_SMALL_LAST_FRAME) {
            if (!impl_->EncodeFrame(remaining, callback)) {
                return false;
            }
        } else {
            impl_->pending.resize(max_samples, 0.0f);
            if (!impl_->EncodeFrame(frame_size, callback)) {
                return false;
            }
        }
    }
    if (!impl_->SendFrame(nullptr, callback)) {
        return false;
    }
    impl_->flushed = true;
    return true;
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

int AacEncoder::sample_rate() const {
    return impl_ && impl_->codec ? impl_->sample_rate : 0;
}

int AacEncoder::channels() const {
    return impl_ && impl_->codec ? impl_->channels : 0;
}

int AacEncoder::bitrate() const {
    return impl_ && impl_->codec ? impl_->bitrate : 0;
}

int AacEncoder::frame_size() const {
    return impl_ && impl_->codec ? impl_->codec->frame_size : 0;
}

int AacEncoder::initial_padding_samples() const {
    return impl_ && impl_->codec ? impl_->codec->initial_padding : 0;
}

const std::vector<uint8_t>& AacEncoder::extradata() const {
    static const std::vector<uint8_t> empty;
    return impl_ ? impl_->extradata : empty;
}

}  // namespace hmmob
