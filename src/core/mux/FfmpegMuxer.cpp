#include "core/mux/FfmpegMuxer.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
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
    std::string codec;
    bool header_written = false;
    bool write_failed = false;
};

FfmpegMuxer::~FfmpegMuxer() {
    Close();
}

bool FfmpegMuxer::Open(const std::string& path, const std::string& codec,
                       int width, int height, int fps, int bitrate) {
    Close();
    if (path.empty() || !IsSupportedCodec(codec) || width <= 0 || height <= 0 ||
        fps <= 0 || bitrate <= 0) {
        std::fprintf(stderr, "FfmpegMuxer: invalid Open arguments\n");
        return false;
    }

    impl_ = new Impl();
    impl_->codec = codec;

    int result = avformat_alloc_output_context2(&impl_->format, nullptr, "mp4",
                                                path.c_str());
    if (result < 0 || !impl_->format) {
        LogAvError("avformat_alloc_output_context2", result);
        Close();
        return false;
    }

    impl_->stream = avformat_new_stream(impl_->format, nullptr);
    if (!impl_->stream) {
        std::fprintf(stderr, "FfmpegMuxer: avformat_new_stream failed\n");
        Close();
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

    result = avio_open(&impl_->format->pb, path.c_str(), AVIO_FLAG_WRITE);
    if (result < 0) {
        LogAvError("avio_open", result);
        Close();
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

bool FfmpegMuxer::Close() {
    if (!impl_) {
        return true;
    }

    // A session that never wrote a header produced no MP4, and a latched write
    // failure means the output is incomplete; both are Close failures.
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
