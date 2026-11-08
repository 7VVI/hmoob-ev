#include "core/mux/FfmpegMuxer.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/error.h>
#include <libavutil/macros.h>
#include <libavutil/mem.h>
#include <libavutil/opt.h>
}

#include <cctype>
#include <cstdio>
#include <cstring>
#include <string>
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
// Both audio backends emit 1024-sample packets.
constexpr int kAudioFrameSize = 1024;

enum class Container { Mp4, Mov, Mkv, Avi, Flv, Wmv };

struct ContainerInfo {
    Container container;
    const char* muxer;
    const char* extension;
    const char* name;
};

constexpr ContainerInfo kContainers[] = {
    {Container::Mp4, "mp4", ".mp4", "MP4"},
    {Container::Mov, "mov", ".mov", "MOV"},
    {Container::Mkv, "matroska", ".mkv", "MKV"},
    {Container::Avi, "avi", ".avi", "AVI"},
    {Container::Flv, "flv", ".flv", "FLV"},
    {Container::Wmv, "asf", ".wmv", "WMV"},
};

bool IsSupportedCodec(const std::string& codec) {
    return codec == "hevc" || codec == "h264";
}

AVCodecID CodecIdFor(const std::string& codec) {
    return codec == "hevc" ? AV_CODEC_ID_HEVC : AV_CODEC_ID_H264;
}

// Lowercased extension including the dot, or empty when the path has none.
std::string LowerExtension(const std::string& path) {
    const size_t slash = path.find_last_of("/\\");
    const size_t dot = path.find_last_of('.');
    if (dot == std::string::npos ||
        (slash != std::string::npos && dot < slash)) {
        return {};
    }
    std::string extension = path.substr(dot);
    for (char& character : extension) {
        character = static_cast<char>(
            std::tolower(static_cast<unsigned char>(character)));
    }
    return extension;
}

const ContainerInfo* FindContainer(const std::string& path) {
    const std::string extension = LowerExtension(path);
    if (extension.empty()) {
        return nullptr;
    }
    for (const ContainerInfo& info : kContainers) {
        if (extension == info.extension) {
            return &info;
        }
    }
    return nullptr;
}

const char* ContainerName(Container container) {
    for (const ContainerInfo& info : kContainers) {
        if (info.container == container) {
            return info.name;
        }
    }
    return "unknown";
}

const char* MuxerName(Container container) {
    for (const ContainerInfo& info : kContainers) {
        if (info.container == container) {
            return info.muxer;
        }
    }
    return "mp4";
}

bool SupportsFastStart(Container container) {
    return container == Container::Mp4 || container == Container::Mov;
}

// AVI stores H.264 as an Annex B byte stream (avienc rejects AVC length-
// prefixed samples), and ASF follows the same convention. Matroska, MP4/MOV
// and FLV take 4-byte length-prefixed samples with avcC/hvcC extradata.
bool UsesAnnexB(Container container) {
    return container == Container::Avi || container == Container::Wmv;
}

// Annex B parameter sets for the containers that cannot carry avcC.
std::vector<uint8_t> BuildAnnexBExtradata(const std::string& codec,
                                          const uint8_t* data, size_t size) {
    const bool hevc = codec == "hevc";
    const std::vector<detail::NalUnit> nals =
        detail::ParseAnnexB(data, size, hevc);
    constexpr uint8_t kStartCode[] = {0x00, 0x00, 0x00, 0x01};
    std::vector<uint8_t> out;
    for (const detail::NalUnit& nal : nals) {
        if (!detail::IsParameterSetNal(nal.type, hevc)) {
            continue;
        }
        out.insert(out.end(), kStartCode, kStartCode + sizeof(kStartCode));
        out.insert(out.end(), nal.data, nal.data + nal.size);
    }
    return out;
}

// Selects the FourCC each container expects for H.264/HEVC. MP4/MOV want
// avc1/hvc1, AVI/ASF use their H264 FourCC, and Matroska/FLV derive the codec
// from the track type and extradata instead.
uint32_t VideoCodecTag(Container container, const std::string& codec) {
    const bool hevc = codec == "hevc";
    switch (container) {
        case Container::Mp4:
        case Container::Mov:
            return hevc ? MKTAG('h', 'v', 'c', '1') : MKTAG('a', 'v', 'c', '1');
        case Container::Avi:
        case Container::Wmv:
            return MKTAG('H', '2', '6', '4');
        case Container::Mkv:
        case Container::Flv:
            return 0;
    }
    return 0;
}

// Fails Open with a readable diagnostic for combinations a container cannot
// represent.
bool ValidateCombination(Container container, const std::string& codec,
                         const AudioConfig& audio) {
    const bool hevc = codec == "hevc";
    const bool pcm = audio.enabled && audio.codec == AudioCodec::PcmS16Le;
    if (hevc && (container == Container::Avi || container == Container::Flv ||
                 container == Container::Wmv)) {
        std::fprintf(stderr,
                     "FfmpegMuxer: HEVC is not supported in the %s container; "
                     "use H.264\n",
                     ContainerName(container));
        return false;
    }
    if (pcm && container == Container::Flv) {
        std::fprintf(stderr,
                     "FfmpegMuxer: the FLV container does not support PCM "
                     "audio; use AAC\n");
        return false;
    }
    return true;
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
    Container container = Container::Mp4;
    int fps = 0;
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

    const ContainerInfo* container_info = FindContainer(path);
    if (!container_info) {
        const std::string extension = LowerExtension(path);
        std::fprintf(stderr,
                     "FfmpegMuxer: unsupported output extension \"%s\"; use "
                     ".mp4, .mov, .mkv, .avi, .flv or .wmv\n",
                     extension.empty() ? "(none)" : extension.c_str());
        return false;
    }
    if (!ValidateCombination(container_info->container, codec, audio)) {
        return false;
    }

    const bool pcm_audio =
        audio.enabled && audio.codec == AudioCodec::PcmS16Le;
    if (audio.enabled) {
        if (audio.sample_rate != 44100 && audio.sample_rate != 48000) {
            std::fprintf(stderr,
                         "FfmpegMuxer: audio sample rate %d is not supported; "
                         "use 44100 or 48000\n",
                         audio.sample_rate);
            return false;
        }
        if (audio.channels != 1 && audio.channels != 2) {
            std::fprintf(stderr,
                         "FfmpegMuxer: unsupported audio channel count %d; "
                         "use mono or stereo\n",
                         audio.channels);
            return false;
        }
        if (audio.bitrate <= 0) {
            std::fprintf(stderr, "FfmpegMuxer: invalid audio bitrate %d\n",
                         audio.bitrate);
            return false;
        }
        if (!pcm_audio &&
            (audio.frame_size <= 0 || audio.extradata.empty())) {
            std::fprintf(
                stderr,
                "FfmpegMuxer: AAC audio requires a frame size and "
                "AudioSpecificConfig\n");
            return false;
        }
    }

    impl_ = new Impl();
    impl_->codec = codec;
    impl_->container = container_info->container;
    impl_->fps = fps;

    int result = avformat_alloc_output_context2(
        &impl_->format, nullptr, container_info->muxer, path.c_str());
    if (result < 0 || !impl_->format) {
        LogAvError("avformat_alloc_output_context2", result);
        std::fprintf(stderr, "FfmpegMuxer: %s muxer unavailable\n",
                     ContainerName(impl_->container));
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
    // avc1/hvc1 (and the AVI/ASF FourCC) declare that parameter sets live in
    // the extradata; the muxer strips them from the samples to match.
    parameters->codec_tag = VideoCodecTag(impl_->container, codec);
    parameters->width = width;
    parameters->height = height;
    parameters->format = AV_PIX_FMT_YUV420P;
    parameters->bit_rate = bitrate;

    // Microseconds match the timestamps AMF reports through AmfPacket. AVI
    // derives its frame rate from the video time base, so it uses 1/fps.
    impl_->stream->time_base = impl_->container == Container::Avi
                                   ? AVRational{1, fps}
                                   : kMicrosecondTimeBase;
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
        audio_parameters->codec_id =
            pcm_audio ? AV_CODEC_ID_PCM_S16LE : AV_CODEC_ID_AAC;
        audio_parameters->sample_rate = audio.sample_rate;
        av_channel_layout_default(&audio_parameters->ch_layout, audio.channels);
        if (audio_parameters->ch_layout.nb_channels != audio.channels) {
            std::fprintf(stderr,
                         "FfmpegMuxer: unsupported audio channel count %d\n",
                         audio.channels);
            static_cast<void>(Close());
            return false;
        }
        audio_parameters->format =
            pcm_audio ? AV_SAMPLE_FMT_S16 : AV_SAMPLE_FMT_NONE;
        // AVI derives its audio dwScale from codecpar->frame_size. A PCM
        // "sample" there is one block (four bytes for stereo s16), while AAC
        // packets really are 1024 samples, so the two differ.
        const int audio_frame_size =
            pcm_audio
                ? (impl_->container == Container::Avi ? 1 : kAudioFrameSize)
                : audio.frame_size;
        audio_parameters->frame_size = audio_frame_size;
        audio_parameters->initial_padding =
            pcm_audio ? 0 : audio.initial_padding;
        audio_parameters->bit_rate =
            pcm_audio
                ? audio.sample_rate * audio.channels * 16
                : audio.bitrate;
        audio_parameters->codec_tag = 0;
        if (!pcm_audio && !SetExtradata(audio_parameters, audio.extradata)) {
            static_cast<void>(Close());
            return false;
        }
        // Audio timestamps are expressed in samples, except in AVI where the
        // muxer counts units of frame_size samples (dwScale / dwRate).
        impl_->audio_stream->time_base =
            impl_->container == Container::Avi
                ? AVRational{audio_frame_size, audio.sample_rate}
                : AVRational{1, audio.sample_rate};
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
            UsesAnnexB(impl_->container)
                ? BuildAnnexBExtradata(impl_->codec, data, size)
                : BuildCodecExtradata(impl_->codec, data, size);
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

        if (SupportsFastStart(impl_->container)) {
            // Move the moov atom to the front so the file is playable before
            // it is fully downloaded. A build without the option is not fatal.
            const int faststart = av_opt_set(impl_->format->priv_data,
                                             "movflags", "+faststart", 0);
            if (faststart < 0) {
                std::fprintf(stderr,
                             "FfmpegMuxer: faststart unavailable (%d); "
                             "writing a plain %s\n",
                             faststart, ContainerName(impl_->container));
            }
        }
        const int result = avformat_write_header(impl_->format, nullptr);
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
    std::vector<uint8_t> payload;
    if (UsesAnnexB(impl_->container)) {
        // AVI/ASF carry the original Annex B access unit, SPS/PPS included.
        if (detail::ParseAnnexB(data, size, hevc).empty()) {
            std::fprintf(stderr, "FfmpegMuxer: packet contains no NAL units\n");
            impl_->write_failed = true;
            return false;
        }
        payload.assign(data, data + size);
    } else {
        payload = detail::AnnexBToLengthPrefixed(data, size, hevc);
    }
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
    // Some muxers (AVI, FLV, Matroska) use the last packet's duration for the
    // container duration, so state the frame interval explicitly.
    packet->duration = 1'000'000 / impl_->fps;
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
    // Express the duration in microseconds first so the shared rescale below
    // produces the right value in any stream time base (AVI counts blocks).
    const int audio_sample_rate =
        impl_->audio_stream->codecpar->sample_rate;
    packet->duration =
        audio_sample_rate > 0
            ? duration_samples * 1'000'000 / audio_sample_rate
            : 0;
    // A negative first PTS (AAC priming) is preserved: movenc turns it into
    // the MP4 edit list instead of us clamping it to zero. PCM starts at 0.
    av_packet_rescale_ts(packet, kMicrosecondTimeBase, impl_->audio_stream->time_base);

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

    // A session that never wrote a header produced no output (including when
    // only audio was buffered), and a latched write failure means the output
    // is incomplete; both are Close failures.
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
