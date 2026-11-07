#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace hmmob {

// Parses an Annex B access unit and builds avcC (H.264) or hvcC (HEVC).
// Returns empty if the required SPS/PPS/VPS NAL units are not present.
//
// Preconditions:
//  - H.264: the SPS may use Baseline, Main, or High profile. High profiles
//    (profile_idc 100/110/122/144/244) get the avcC High-profile extension
//    describing 8-bit 4:2:0.
//  - HEVC: only Main profile (general_profile_idc == 1, 8-bit 4:2:0) is
//    supported. Other profiles return empty because the generated hvcC would
//    misdescribe the stream's chroma format and bit depths.
std::vector<uint8_t> BuildCodecExtradata(const std::string& codec,
                                         const uint8_t* data, size_t size);

}  // namespace hmmob
