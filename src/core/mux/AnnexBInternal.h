#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

// Internal helpers shared by the extradata builder and the MP4 muxer. This
// header is not part of the public hmmob surface.
namespace hmmob::detail {

// A NAL unit inside an Annex B byte stream. `data` points into the caller's
// buffer and is valid only while that buffer is alive.
struct NalUnit {
    int type = 0;
    const uint8_t* data = nullptr;
    size_t size = 0;
};

// Splits an Annex B access unit into NAL units. Recognizes both 3-byte and
// 4-byte start codes and trims the zero padding that precedes each start code.
std::vector<NalUnit> ParseAnnexB(const uint8_t* data, size_t size, bool hevc);

// True for SPS/PPS (H.264) or VPS/SPS/PPS (HEVC); MP4 carries these in the
// avcC/hvcC extradata rather than in the samples.
bool IsParameterSetNal(int type, bool hevc);

// Converts Annex B NAL units to the 4-byte length-prefixed form MP4 requires.
// Parameter-set NAL units are dropped when other NAL units remain, since the
// extradata already carries them. Returns an empty vector on failure.
std::vector<uint8_t> AnnexBToLengthPrefixed(const uint8_t* data, size_t size,
                                            bool hevc);

}  // namespace hmmob::detail
