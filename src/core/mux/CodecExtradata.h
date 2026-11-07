#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace hmmob {

// Parses an Annex B access unit and builds avcC (H.264) or hvcC (HEVC).
// Returns empty if the required SPS/PPS/VPS NAL units are not present.
std::vector<uint8_t> BuildCodecExtradata(const std::string& codec,
                                         const uint8_t* data, size_t size);

}  // namespace hmmob
