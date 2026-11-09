#include "app/VideoThumbnail.h"

#include <QByteArray>

#include <cmath>
#include <memory>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
}

namespace {

struct FormatCloser {
    void operator()(AVFormatContext* context) const {
        if (context) {
            avformat_close_input(&context);
        }
    }
};

struct CodecCloser {
    void operator()(AVCodecContext* context) const {
        avcodec_free_context(&context);
    }
};

struct PacketDeleter {
    void operator()(AVPacket* packet) const { av_packet_free(&packet); }
};

struct FrameDeleter {
    void operator()(AVFrame* frame) const { av_frame_free(&frame); }
};

struct SwsCloser {
    void operator()(SwsContext* context) const { sws_freeContext(context); }
};

using FormatPtr = std::unique_ptr<AVFormatContext, FormatCloser>;
using CodecPtr = std::unique_ptr<AVCodecContext, CodecCloser>;
using PacketPtr = std::unique_ptr<AVPacket, PacketDeleter>;
using FramePtr = std::unique_ptr<AVFrame, FrameDeleter>;
using SwsPtr = std::unique_ptr<SwsContext, SwsCloser>;

// Opens the container and returns the best video stream, or nullptr.
FormatPtr OpenVideo(const QString& path, int& stream_index) {
    const QByteArray encoded = path.toUtf8();
    AVFormatContext* raw = nullptr;
    if (avformat_open_input(&raw, encoded.constData(), nullptr, nullptr) < 0) {
        return nullptr;
    }
    FormatPtr format(raw);
    if (avformat_find_stream_info(format.get(), nullptr) < 0) {
        return nullptr;
    }
    stream_index =
        av_find_best_stream(format.get(), AVMEDIA_TYPE_VIDEO, -1, -1, nullptr,
                            0);
    if (stream_index < 0) {
        return nullptr;
    }
    return format;
}

double StreamDurationSeconds(const AVFormatContext* format,
                             const AVStream* stream) {
    if (format->duration > 0 && format->duration != AV_NOPTS_VALUE) {
        return static_cast<double>(format->duration) / AV_TIME_BASE;
    }
    if (stream->duration > 0 && stream->duration != AV_NOPTS_VALUE) {
        return static_cast<double>(stream->duration) *
               av_q2d(stream->time_base);
    }
    return 0.0;
}

}  // namespace

bool ReadVideoInfo(const QString& path, VideoInfo& info) {
    info = VideoInfo();
    if (path.isEmpty()) {
        return false;
    }
    int stream_index = -1;
    FormatPtr format = OpenVideo(path, stream_index);
    if (!format) {
        return false;
    }
    const AVStream* stream = format->streams[stream_index];
    info.width = stream->codecpar->width;
    info.height = stream->codecpar->height;
    info.duration_seconds = StreamDurationSeconds(format.get(), stream);
    return info.width > 0 && info.height > 0;
}

QImage DecodeThumbnail(const QString& path, int width, int height) {
    if (path.isEmpty() || width <= 0 || height <= 0) {
        return QImage();
    }
    int stream_index = -1;
    FormatPtr format = OpenVideo(path, stream_index);
    if (!format) {
        return QImage();
    }
    const AVStream* stream = format->streams[stream_index];
    const AVCodec* decoder =
        avcodec_find_decoder(stream->codecpar->codec_id);
    if (!decoder) {
        return QImage();
    }
    CodecPtr codec(avcodec_alloc_context3(decoder));
    if (!codec ||
        avcodec_parameters_to_context(codec.get(), stream->codecpar) < 0) {
        return QImage();
    }
    if (avcodec_open2(codec.get(), decoder, nullptr) < 0) {
        return QImage();
    }

    PacketPtr packet(av_packet_alloc());
    FramePtr frame(av_frame_alloc());
    if (!packet || !frame) {
        return QImage();
    }

    bool decoded = false;
    while (!decoded && av_read_frame(format.get(), packet.get()) >= 0) {
        if (packet->stream_index != stream_index) {
            av_packet_unref(packet.get());
            continue;
        }
        int sent = avcodec_send_packet(codec.get(), packet.get());
        av_packet_unref(packet.get());
        if (sent < 0 && sent != AVERROR(EAGAIN)) {
            break;
        }
        for (;;) {
            const int received = avcodec_receive_frame(codec.get(), frame.get());
            if (received == 0) {
                decoded = true;
                break;
            }
            if (received == AVERROR(EAGAIN)) {
                break;
            }
            return QImage();
        }
    }

    if (!decoded) {
        // Small or single-frame clips may only produce a frame after a flush.
        if (avcodec_send_packet(codec.get(), nullptr) == 0 &&
            avcodec_receive_frame(codec.get(), frame.get()) == 0) {
            decoded = true;
        }
    }
    if (!decoded || frame->width <= 0 || frame->height <= 0 ||
        frame->format == AV_PIX_FMT_NONE) {
        return QImage();
    }

    // Cover the target: scale the whole frame uniformly so it is at least as
    // large as the target in both axes, then crop the centre. This avoids
    // per-plane offsets when the source uses chroma subsampling.
    const double target_ratio =
        static_cast<double>(width) / static_cast<double>(height);
    const double source_ratio =
        static_cast<double>(frame->width) / static_cast<double>(frame->height);
    int scaled_width = width;
    int scaled_height = height;
    if (source_ratio > target_ratio) {
        scaled_height = height;
        scaled_width = qMax(width, qRound(height * source_ratio));
    } else {
        scaled_width = width;
        scaled_height = qMax(height, qRound(width / source_ratio));
    }

    QImage scaled(scaled_width, scaled_height, QImage::Format_RGB32);
    if (scaled.isNull()) {
        return QImage();
    }
    SwsPtr sws(sws_getContext(
        frame->width, frame->height,
        static_cast<AVPixelFormat>(frame->format), scaled_width,
        scaled_height, AV_PIX_FMT_BGRA, SWS_BILINEAR, nullptr, nullptr,
        nullptr));
    if (!sws) {
        return QImage();
    }
    uint8_t* destination[4] = {scaled.bits(), nullptr, nullptr, nullptr};
    int destination_linesize[4] = {static_cast<int>(scaled.bytesPerLine()), 0,
                                   0, 0};
    const int converted = sws_scale(sws.get(), frame->data, frame->linesize, 0,
                                    frame->height, destination,
                                    destination_linesize);
    if (converted <= 0) {
        return QImage();
    }

    const int crop_x = (scaled_width - width) / 2;
    const int crop_y = (scaled_height - height) / 2;
    if (crop_x == 0 && crop_y == 0 && scaled_width == width &&
        scaled_height == height) {
        return scaled;
    }
    return scaled.copy(crop_x, crop_y, width, height);
}
