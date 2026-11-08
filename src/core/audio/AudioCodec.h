#pragma once

namespace hmmob {

// Audio encoding format shared by the encoder and the muxer: AudioEncoder
// produces one of these and FfmpegMuxer::AudioConfig declares it.
enum class AudioCodec {
    Aac,
    PcmS16Le,
};

}  // namespace hmmob
