#include "core/mux/FfmpegMuxer.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/error.h>
#include <libavutil/macros.h>
#include <libavutil/mem.h>
}

#include <cstdio>
#include <cstring>
#include <vector>

#include "core/mux/AnnexBInternal.h"
#include "core/mux/CodecExtradata.h"

// Fail loudly if the vcpkg FFmpeg baseline moves without a deliberate review.
static_assert(LIBAVCODEC_VERSION_MAJOR == 63,
              "Expected FFmpeg 9.x (libavcodec 63); update the vcpkg baseline deliberately");
static_assert(LIBAVFORMAT_VERSION_MAJOR == 63,
              "Expected FFmpeg 9.x (libavformat 63); update the vcpkg baseline deliberately");

namespace hmmob {
namespace {

constexpr AVRational kMicrosecondTimeBase{1, 1000000};

bool IsSupportedCodec(const std::string& codec) {
    return codec == "hevc" || codec == "h264";
}

AVCodecID CodecIdFor(const std::string& codec) {
    return codec == "hevc" ? AV_CODEC_ID_HEVC : AV_CODEC_ID_H264;
}

void LogAvError(const char* what, int error) {
    char message[AV_ERROR_MAX_STRING_SIZE] = {};
    av_strerror(error, message, sizeof(message));
    std::fprintf(stderr, "FfmpegMuxer: %s failed: %s (%d)\n", what, message, error);
}

bool SetExtradata(AVCodecParameters* parameters,
                  const std::vector<uint8_t>& extradata) {
    parameters->extradata = static_cast<uint8_t*>(
        av_mallocz(extradata.size() + AV_INPUT_BUFFER_PADDING_SIZE));
    if (!parameters->extradata) {
        std::fprintf(stderr, "FfmpegMuxer: failed to allocate extradata\n");
        return false;
    }
    std::memcpy(parameters->extradata, extradata.data(), extradata.size());
    parameters->extradata_size = static_cast<int>(extradata.size());
    return true;
}

}  // namespace

struct FfmpegMuxer::Impl {
    AVFormatContext* format = nullptr;
    AVStream* stream = nullptr;
    AVStream* audio_stream = nullptr;
    std::string codec;
    struct PendingAudio {
        std::vector<uint8_t> data;
        int64_t pts_us;
        int duration_samples;
    };
    std::vector<PendingAudio> pending_audio;
    int64_t pending_audio_samples = 0;
    size_t pending_audio_bytes = 0;
    bool header_written = false;
    bool write_failed = false;
};

FfmpegMuxer::~FfmpegMuxer() {
    static_cast<void>(Close());
}

bool FfmpegMuxer::Open(const std::string& path, const std::string& codec,
                       int width, int height, int fps, int bitrate,
                       const AudioConfig& audio) {
    static_cast<void>(Close());
    if (path.empty() || !IsSupportedCodec(codec) || width <= 0 || height <= 0 ||
        fps <= 0 || bitrate <= 0) {
        std::fprintf(stderr, "FfmpegMuxer: invalid Open arguments\n");
        return false;
    }
    if (audio.enabled &&
        (audio.sample_rate <= 0 || audio.channels <= 0 || audio.channels > 8 ||
         audio.bitrate <= 0 || audio.frame_size <= 0)) {
        std::fprintf(stderr, "FfmpegMuxer: invalid audio configuration\n");
        return false;
    }
    if (audio.enabled && audio.extradata.empty()) {
        std::fprintf(stderr,
                     "FfmpegMuxer: audio enabled without AudioSpecificConfig\n");
        return false;
    }

    impl_ = new Impl();
    impl_->codec = codec;

    int result = avformat_alloc_output_context2(&impl_->format, nullptr, "mp4",
                                                path.c_str());
    if (result < 0 || !impl_->format) {
        LogAvError("avformat_alloc_output_context2", result);
        static_cast<void>(Close());
        return false;
    }

    impl_->stream = avformat_new_stream(impl_->format, nullptr);
    if (!impl_->stream) {
        std::fprintf(stderr, "FfmpegMuxer: avformat_new_stream failed\n");
        static_cast<void>(Close());
        return false;
    }

    AVCodecParameters* parameters = impl_->stream->codecpar;
    parameters->codec_type = AVMEDIA_TYPE_VIDEO;
    parameters->codec_id = CodecIdFor(codec);
    // avc1/hvc1 declare that parameter sets live in the extradata; the muxer
    // strips them from the samples to match.
    parameters->codec_tag = codec == "hevc" ? MKTAG('h', 'v', 'c', '1')
                                            : MKTAG('a', 'v', 'c', '1');
    parameters->width = width;
    parameters->height = height;
    parameters->format = AV_PIX_FMT_YUV420P;
    parameters->bit_rate = bitrate;

    // Microseconds match the timestamps AMF reports through AmfPacket.
    impl_->stream->time_base = kMicrosecondTimeBase;
    impl_->stream->avg_frame_rate = AVRational{fps, 1};
    impl_->stream->r_frame_rate = AVRational{fps, 1};

    if (audio.enabled) {
        impl_->audio_stream = avformat_new_stream(impl_->format, nullptr);
        if (!impl_->audio_stream) {
            std::fprintf(stderr, "FfmpegMuxer: avformat_new_stream(audio) failed\n");
            static_cast<void>(Close());
            return false;
        }
        AVCodecParameters* audio_parameters = impl_->audio_stream->codecpar;
        audio_parameters->codec_type = AVMEDIA_TYPE_AUDIO;
        audio_parameters->codec_id = AV_CODEC_ID_AAC;
        audio_parameters->sample_rate = audio.sample_rate;
        av_channel_layout_default(&audio_parameters->ch_layout, audio.channels);
        if (audio_parameters->ch_layout.nb_channels != audio.channels) {
            std::fprintf(stderr,
                         "FfmpegMuxer: unsupported audio channel count %d\n",
                         audio.channels);
            static_cast<void>(Close());
            return false;
        }
        // Encoded AAC is not PCM; leave the sample format unspecified.
        audio_parameters->format = AV_SAMPLE_FMT_NONE;
        audio_parameters->frame_size = audio.frame_size;
        audio_parameters->initial_padding = audio.initial_padding;
        audio_parameters->bit_rate = audio.bitrate;
        audio_parameters->codec_tag = 0;
        // Audio is rejected above when extradata is empty.
        if (!SetExtradata(audio_parameters, audio.extradata)) {
            static_cast<void>(Close());
            return false;
        }
        // AAC timestamps are expressed in samples.
        impl_->audio_stream->time_base = AVRational{1, audio.sample_rate};
    }

    result = avio_open(&impl_->format->pb, path.c_str(), AVIO_FLAG_WRITE);
    if (result < 0) {
        LogAvError("avio_open", result);
        static_cast<void>(Close());
        return false;
    }

    // The header is written by the first WritePacket, once the codec
    // extradata from the SPS/PPS NAL units is known.
    return true;
}

bool FfmpegMuxer::WritePacket(const uint8_t* data, size_t size,
                              int64_t pts_us, bool keyframe) {
    if (!impl_ || !impl_->format || !impl_->stream || impl_->write_failed) {
        return false;
    }
    if (!data || size == 0) {
        std::fprintf(stderr, "FfmpegMuxer: WritePacket called without data\n");
        impl_->write_failed = true;
        return false;
    }

    if (!impl_->header_written) {
        const std::vector<uint8_t> extradata =
            BuildCodecExtradata(impl_->codec, data, size);
        if (extradata.empty()) {
            std::fprintf(stderr,
                         "FfmpegMuxer: first %s packet is missing SPS/PPS/VPS\n",
                         impl_->codec.c_str());
            impl_->write_failed = true;
            return false;
        }
        if (!SetExtradata(impl_->stream->codecpar, extradata)) {
            impl_->write_failed = true;
            return false;
        }

        AVDictionary* options = nullptr;
        av_dict_set(&options, "movflags", "+faststart", 0);
        const int result = avformat_write_header(impl_->format, &options);
        av_dict_free(&options);
        if (result < 0) {
            LogAvError("avformat_write_header", result);
            impl_->write_failed = true;
            return false;
        }
        impl_->header_written = true;

        // Audio packets that arrived before the header was available can now
        // be written. WriteAudioPacket sees header_written == true and muxes
        // them directly; a failure there latches the session.
        for (const auto& pending : impl_->pending_audio) {
            if (!WriteAudioPacket(pending.data.data(), pending.data.size(),
                                  pending.pts_us, pending.duration_samples)) {
                return false;
            }
        }
        impl_->pending_audio.clear();
        impl_->pending_audio_samples = 0;
        impl_->pending_audio_bytes = 0;
    }

    const bool hevc = impl_->codec == "hevc";
    const std::vector<uint8_t> payload =
        detail::AnnexBToLengthPrefixed(data, size, hevc);
    if (payload.empty()) {
        std::fprintf(stderr, "FfmpegMuxer: packet contains no NAL units\n");
        impl_->write_failed = true;
        return false;
    }

    AVPacket* packet = av_packet_alloc();
    if (!packet) {
        impl_->write_failed = true;
        return false;
    }
    if (av_new_packet(packet, static_cast<int>(payload.size())) < 0) {
        av_packet_free(&packet);
        impl_->write_failed = true;
        return false;
    }
    std::memcpy(packet->data, payload.data(), payload.size());
    packet->pts = pts_us;
    packet->dts = pts_us;
    packet->stream_index = impl_->stream->index;
    packet->flags = keyframe ? AV_PKT_FLAG_KEY : 0;
    av_packet_rescale_ts(packet, kMicrosecondTimeBase, impl_->stream->time_base);

    const int result = av_interleaved_write_frame(impl_->format, packet);
    av_packet_free(&packet);
    if (result < 0) {
        LogAvError("av_interleaved_write_frame", result);
        impl_->write_failed = true;
        return false;
    }
    return true;
}

bool FfmpegMuxer::WriteAudioPacket(const uint8_t* data, size_t size,
                                   int64_t pts_us, int duration_samples) {
    if (!impl_ || !impl_->format || impl_->write_failed) {
        return false;
    }
    if (!impl_->audio_stream) {
        std::fprintf(stderr,
                     "FfmpegMuxer: WriteAudioPacket called without an audio stream\n");
        return false;
    }
    if (!data || size == 0) {
        std::fprintf(stderr, "FfmpegMuxer: WriteAudioPacket called without data\n");
        impl_->write_failed = true;
        return false;
    }

    if (!impl_->header_written) {
        // Bound the pre-header buffer by time, samples, packet count and
        // bytes so non-monotonic or bogus timestamps cannot grow it without
        // limit.
        constexpr int64_t kMaxPendingUs = 5'000'000;
        const AVCodecParameters* parameters = impl_->audio_stream->codecpar;
        const int64_t max_samples = int64_t{5} * parameters->sample_rate;
        const size_t max_packets =
            parameters->frame_size > 0
                ? static_cast<size_t>(max_samples / parameters->frame_size) + 2
                : 1024;
        const int64_t max_bytes =
            int64_t{5} * parameters->bit_rate / 8 + 65536;
        const bool too_old =
            !impl_->pending_audio.empty() &&
            pts_us - impl_->pending_audio.front().pts_us > kMaxPendingUs;
        const bool too_many_samples =
            impl_->pending_audio_samples + duration_samples > max_samples;
        const bool too_many_packets =
            impl_->pending_audio.size() + 1 > max_packets;
        const bool too_many_bytes =
            impl_->pending_audio_bytes + size > static_cast<size_t>(max_bytes);
        if (too_old || too_many_samples || too_many_packets || too_many_bytes) {
            std::fprintf(stderr,
                         "FfmpegMuxer: audio buffer overflow before video header\n");
            impl_->write_failed = true;
            return false;
        }
        impl_->pending_audio.push_back(
            {std::vector<uint8_t>(data, data + size), pts_us, duration_samples});
        impl_->pending_audio_samples += duration_samples;
        impl_->pending_audio_bytes += size;
        return true;
    }

    AVPacket* packet = av_packet_alloc();
    if (!packet) {
        std::fprintf(stderr, "FfmpegMuxer: failed to allocate audio packet\n");
        impl_->write_failed = true;
        return false;
    }
    if (av_new_packet(packet, static_cast<int>(size)) < 0) {
        std::fprintf(stderr, "FfmpegMuxer: failed to allocate audio packet data\n");
        av_packet_free(&packet);
        impl_->write_failed = true;
        return false;
    }
    std::memcpy(packet->data, data, size);
    packet->pts = pts_us;
    packet->dts = pts_us;
    packet->stream_index = impl_->audio_stream->index;
    // The negative first PTS (AAC priming) is preserved: movenc turns it into
    // the MP4 edit list instead of us clamping it to zero.
    av_packet_rescale_ts(packet, kMicrosecondTimeBase, impl_->audio_stream->time_base);
    // After rescaling, the stream time base is 1/sample_rate, so the encoder's
    // sample count is already a valid duration and must not be rescaled.
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

bool FfmpegMuxer::Close() {
    if (!impl_) {
        return true;
    }

    // A session that never wrote a header produced no MP4 (including when only
    // audio was buffered), and a latched write failure means the output is
    // incomplete; both are Close failures.
    bool ok = impl_->header_written && !impl_->write_failed;
    if (!impl_->header_written && impl_->format && impl_->format->pb) {
        std::fprintf(stderr, "FfmpegMuxer: closing without a written header\n");
    }
    if (impl_->format) {
        if (impl_->header_written) {
            const int result = av_write_trailer(impl_->format);
            if (result < 0) {
                LogAvError("av_write_trailer", result);
                ok = false;
            }
        }
        if (impl_->format->pb) {
            if (avio_closep(&impl_->format->pb) < 0) {
                ok = false;
            }
        }
        avformat_free_context(impl_->format);
    }
    delete impl_;
    impl_ = nullptr;
    return ok;
}

}  // namespace hmmob
