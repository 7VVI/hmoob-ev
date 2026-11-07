#include "core/audio/AacEncoder.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
#include <libavutil/samplefmt.h>
#include <libswresample/swresample.h>
}

#include <algorithm>
#include <cstddef>
#include <cstdio>

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
    // Interleaved float32 samples not yet handed to the encoder.
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

    // Encodes one frame from the front of `pending`, which must hold at least
    // frame_size frames. The input is consumed whether or not swr buffered a
    // little output delay; pts advance by the number of output samples.
    bool EncodeFrame(const PacketCallback& callback) {
        const int available = static_cast<int>(pending.size() / channels);
        if (available <= 0) {
            return false;
        }
        // Feed exactly one encoder frame. Passing the whole pending buffer here
        // would leave the rest buffered inside swr while we drop it below.
        const int in_frames = std::min(available, codec->frame_size);
        av_frame_unref(frame);
        frame->nb_samples = codec->frame_size;
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
            swr, frame->data, frame->nb_samples, input, in_frames);
        if (converted < 0) {
            LogAv("swr_convert", converted);
            return false;
        }
        if (converted == 0) {
            LogAv("swr_convert", AVERROR(EINVAL));
            return false;
        }
        frame->nb_samples = converted;
        if (!SendFrame(frame, callback)) {
            return false;
        }
        next_pts_samples += converted;
        pending.erase(pending.begin(),
                      pending.begin() + static_cast<std::ptrdiff_t>(in_frames) * channels);
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
    impl_->codec->sample_rate = sample_rate;
    impl_->codec->bit_rate = bitrate;
    impl_->codec->sample_fmt = AV_SAMPLE_FMT_FLTP;
    impl_->codec->profile = AV_PROFILE_AAC_LOW;
    // MP4 wants raw AAC access units plus AudioSpecificConfig in extradata,
    // which the native encoder only emits in global-header mode.
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
    if (!impl_ || !impl_->codec || !interleaved || frames <= 0 || !callback) {
        return false;
    }
    impl_->pending.insert(
        impl_->pending.end(), interleaved,
        interleaved + static_cast<size_t>(frames) * static_cast<size_t>(impl_->channels));
    const int frame_size = impl_->codec->frame_size;
    while (static_cast<int>(impl_->pending.size() /
                            static_cast<size_t>(impl_->channels)) >= frame_size) {
        if (!impl_->EncodeFrame(callback)) {
            return false;
        }
    }
    return true;
}

bool AacEncoder::Flush(const PacketCallback& callback) {
    if (!impl_ || !impl_->codec || !callback) {
        return false;
    }
    if (!impl_->pending.empty()) {
        const size_t target = static_cast<size_t>(impl_->codec->frame_size) *
                              static_cast<size_t>(impl_->channels);
        if (impl_->pending.size() < target) {
            impl_->pending.resize(target, 0.0f);
        }
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
