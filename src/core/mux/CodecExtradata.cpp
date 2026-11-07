#include "core/mux/CodecExtradata.h"

#include <vector>

#include "core/mux/AnnexBInternal.h"

namespace hmmob {
namespace {

constexpr size_t kMaxNalSize = 0xFFFF;

class BitReader {
public:
    BitReader(const uint8_t* data, size_t size) : data_(data), size_(size) {}

    uint64_t Read(int count) {
        uint64_t value = 0;
        for (int i = 0; i < count; ++i) {
            if (bit_pos_ >= size_ * 8) {
                ok_ = false;
                return value;
            }
            const int shift = 7 - static_cast<int>(bit_pos_ % 8);
            value = (value << 1) |
                    ((data_[bit_pos_ / 8] >> shift) & 0x01);
            ++bit_pos_;
        }
        return value;
    }

    [[nodiscard]] bool ok() const { return ok_; }

private:
    const uint8_t* data_ = nullptr;
    size_t size_ = 0;
    size_t bit_pos_ = 0;
    bool ok_ = true;
};

// Removes emulation prevention bytes (00 00 03 -> 00 00) from an RBSP.
std::vector<uint8_t> UnescapeRbsp(const uint8_t* data, size_t size) {
    std::vector<uint8_t> rbsp;
    rbsp.reserve(size);
    int zeros = 0;
    for (size_t i = 0; i < size; ++i) {
        const uint8_t byte = data[i];
        if (zeros >= 2 && byte == 0x03) {
            zeros = 0;
            continue;
        }
        rbsp.push_back(byte);
        zeros = byte == 0x00 ? zeros + 1 : 0;
    }
    return rbsp;
}

void AppendBigEndian(std::vector<uint8_t>& out, uint64_t value, int bytes) {
    for (int shift = (bytes - 1) * 8; shift >= 0; shift -= 8) {
        out.push_back(static_cast<uint8_t>((value >> shift) & 0xFF));
    }
}

void AppendNal(std::vector<uint8_t>& out, const detail::NalUnit& nal) {
    out.push_back(static_cast<uint8_t>(nal.size >> 8));
    out.push_back(static_cast<uint8_t>(nal.size & 0xFF));
    out.insert(out.end(), nal.data, nal.data + nal.size);
}

std::vector<uint8_t> BuildAvcExtradata(const detail::NalUnit& sps,
                                       const detail::NalUnit& pps) {
    // profile_idc, constraint flags and level_idc follow the SPS NAL header.
    if (sps.size < 4 || sps.size > kMaxNalSize || pps.size > kMaxNalSize) {
        return {};
    }

    std::vector<uint8_t> out;
    out.reserve(8 + sps.size + 3 + pps.size);
    out.push_back(1);      // configurationVersion
    out.push_back(sps.data[1]);  // AVCProfileIndication
    out.push_back(sps.data[2]);  // profile_compatibility
    out.push_back(sps.data[3]);  // AVCLevelIndication
    out.push_back(0xFF);   // reserved 6 bits + lengthSizeMinusOne = 3
    out.push_back(0xE1);   // reserved 3 bits + numOfSequenceParameterSets = 1
    AppendNal(out, sps);
    out.push_back(0x01);   // numOfPictureParameterSets
    AppendNal(out, pps);
    return out;
}

std::vector<uint8_t> BuildHevcExtradata(const detail::NalUnit& vps,
                                        const detail::NalUnit& sps,
                                        const detail::NalUnit& pps) {
    if (sps.size < 2 || vps.size > kMaxNalSize || sps.size > kMaxNalSize ||
        pps.size > kMaxNalSize) {
        return {};
    }

    // The HEVC SPS RBSP starts after the 2-byte NAL unit header.
    const std::vector<uint8_t> rbsp = UnescapeRbsp(sps.data + 2, sps.size - 2);
    BitReader reader(rbsp.data(), rbsp.size());

    reader.Read(4);  // sps_video_parameter_set_id
    const uint64_t max_sub_layers_minus1 = reader.Read(3);
    reader.Read(1);  // sps_temporal_id_nesting_flag
    const uint64_t profile_space = reader.Read(2);
    const uint64_t tier_flag = reader.Read(1);
    const uint64_t profile_idc = reader.Read(5);
    const uint64_t compatibility_flags = reader.Read(32);
    const uint64_t constraint_flags = reader.Read(48);
    const uint64_t level_idc = reader.Read(8);

    if (!reader.ok() || max_sub_layers_minus1 > 6) {
        return {};
    }

    std::vector<uint8_t> out;
    out.reserve(23 + 3 * 5 + vps.size + sps.size + pps.size);
    out.push_back(1);  // configurationVersion
    out.push_back(static_cast<uint8_t>((profile_space << 6) |
                                       (tier_flag << 5) | profile_idc));
    AppendBigEndian(out, compatibility_flags, 4);
    AppendBigEndian(out, constraint_flags, 6);
    out.push_back(static_cast<uint8_t>(level_idc));
    out.push_back(0xF0);  // reserved + min_spatial_segmentation_idc (unknown)
    out.push_back(0x00);
    out.push_back(0xFC);  // reserved + parallelismType = 0 (unknown)
    out.push_back(0xFD);  // reserved + chromaFormat = 1 (4:2:0)
    out.push_back(0xF8);  // reserved + bitDepthLumaMinus8 = 0
    out.push_back(0xF8);  // reserved + bitDepthChromaMinus8 = 0
    out.push_back(0x00);  // avgFrameRate
    out.push_back(0x00);
    // constantFrameRate = 0, numTemporalLayers, temporalIdNested = 1,
    // lengthSizeMinusOne = 3 (4-byte NAL lengths).
    out.push_back(static_cast<uint8_t>(
        (((max_sub_layers_minus1 + 1) & 0x07) << 3) | (1 << 2) | 3));
    out.push_back(3);  // numOfArrays

    const detail::NalUnit* arrays[] = {&vps, &sps, &pps};
    for (const detail::NalUnit* nal : arrays) {
        out.push_back(static_cast<uint8_t>(0x80 | nal->type));  // array_completeness
        out.push_back(0x00);  // numNalus, high byte
        out.push_back(0x01);  // numNalus, low byte
        AppendNal(out, *nal);
    }
    return out;
}

}  // namespace

namespace detail {

std::vector<NalUnit> ParseAnnexB(const uint8_t* data, size_t size, bool hevc) {
    std::vector<NalUnit> nals;
    if (!data || size < 3) {
        return nals;
    }

    size_t i = 0;
    while (i + 2 < size) {
        // Locate the next start code and the first byte of its NAL unit.
        size_t start = 0;
        bool found = false;
        for (size_t p = i; p + 2 < size; ++p) {
            if (data[p] == 0x00 && data[p + 1] == 0x00 && data[p + 2] == 0x01) {
                start = p + 3;
                found = true;
                break;
            }
            if (p + 3 < size && data[p] == 0x00 && data[p + 1] == 0x00 &&
                data[p + 2] == 0x00 && data[p + 3] == 0x01) {
                start = p + 4;
                found = true;
                break;
            }
        }
        if (!found || start >= size) {
            break;
        }

        // The NAL ends at the next start code, whose leading zero bytes are
        // indistinguishable from trailing padding; trim them.
        size_t end = size;
        for (size_t p = start; p + 2 < size; ++p) {
            if (data[p] == 0x00 && data[p + 1] == 0x00 &&
                (data[p + 2] == 0x01 ||
                 (p + 3 < size && data[p + 2] == 0x00 && data[p + 3] == 0x01))) {
                end = p;
                break;
            }
        }
        while (end > start && data[end - 1] == 0x00) {
            --end;
        }
        if (end <= start) {
            i = start;
            continue;
        }

        const uint8_t header = data[start];
        NalUnit nal;
        nal.type = hevc ? ((header >> 1) & 0x3F) : (header & 0x1F);
        nal.data = data + start;
        nal.size = end - start;
        nals.push_back(nal);
        i = end;
    }
    return nals;
}

bool IsParameterSetNal(int type, bool hevc) {
    if (hevc) {
        return type == 32 || type == 33 || type == 34;
    }
    return type == 7 || type == 8;
}

std::vector<uint8_t> AnnexBToLengthPrefixed(const uint8_t* data, size_t size,
                                            bool hevc) {
    const std::vector<NalUnit> nals = ParseAnnexB(data, size, hevc);
    if (nals.empty()) {
        return {};
    }

    // Drop parameter sets only when the access unit still has video data;
    // otherwise keep them so the packet is not lost.
    bool keep_parameter_sets = true;
    for (const NalUnit& nal : nals) {
        if (!IsParameterSetNal(nal.type, hevc)) {
            keep_parameter_sets = false;
            break;
        }
    }

    std::vector<uint8_t> out;
    for (const NalUnit& nal : nals) {
        if (!keep_parameter_sets && IsParameterSetNal(nal.type, hevc)) {
            continue;
        }
        if (nal.size > 0xFFFFFFFFu) {
            return {};
        }
        const uint32_t length = static_cast<uint32_t>(nal.size);
        AppendBigEndian(out, length, 4);
        out.insert(out.end(), nal.data, nal.data + nal.size);
    }
    return out;
}

}  // namespace detail

std::vector<uint8_t> BuildCodecExtradata(const std::string& codec,
                                         const uint8_t* data, size_t size) {
    const bool hevc = codec == "hevc";
    if (!hevc && codec != "h264") {
        return {};
    }

    const std::vector<detail::NalUnit> nals = detail::ParseAnnexB(data, size, hevc);
    const detail::NalUnit* vps = nullptr;
    const detail::NalUnit* sps = nullptr;
    const detail::NalUnit* pps = nullptr;
    for (const detail::NalUnit& nal : nals) {
        if (hevc) {
            if (nal.type == 32 && !vps) {
                vps = &nal;
            } else if (nal.type == 33 && !sps) {
                sps = &nal;
            } else if (nal.type == 34 && !pps) {
                pps = &nal;
            }
        } else {
            if (nal.type == 7 && !sps) {
                sps = &nal;
            } else if (nal.type == 8 && !pps) {
                pps = &nal;
            }
        }
    }

    if (!sps || !pps || (hevc && !vps)) {
        return {};
    }
    if (hevc) {
        return BuildHevcExtradata(*vps, *sps, *pps);
    }
    return BuildAvcExtradata(*sps, *pps);
}

}  // namespace hmmob
