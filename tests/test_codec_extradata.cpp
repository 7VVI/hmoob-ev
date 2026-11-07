#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <cstdint>
#include <vector>
#include "core/mux/CodecExtradata.h"

namespace {

using Bytes = std::vector<uint8_t>;

Bytes BuildAnnexB(const std::vector<Bytes>& nals, bool four_byte_start_code = true) {
    Bytes out;
    for (const Bytes& nal : nals) {
        if (four_byte_start_code) {
            out.insert(out.end(), {0x00, 0x00, 0x00, 0x01});
        } else {
            out.insert(out.end(), {0x00, 0x00, 0x01});
        }
        out.insert(out.end(), nal.begin(), nal.end());
    }
    return out;
}

struct HvcArray {
    int type = 0;
    Bytes nal;
};

// Walks the hvcC arrays so the assertions do not depend on raw offsets.
std::vector<HvcArray> ParseHvcArrays(const Bytes& extradata) {
    REQUIRE(extradata.size() > 23);
    std::vector<HvcArray> arrays;
    size_t pos = 23;  // fixed-size HEVCDecoderConfigurationRecord header
    const int num_arrays = extradata[22];
    for (int i = 0; i < num_arrays; ++i) {
        REQUIRE(pos + 3 <= extradata.size());
        HvcArray array;
        array.type = extradata[pos] & 0x3F;
        const int num_nalus = (extradata[pos + 1] << 8) | extradata[pos + 2];
        REQUIRE(num_nalus == 1);
        pos += 3;
        REQUIRE(pos + 2 <= extradata.size());
        const size_t size = (static_cast<size_t>(extradata[pos]) << 8) |
                            static_cast<size_t>(extradata[pos + 1]);
        pos += 2;
        REQUIRE(pos + size <= extradata.size());
        array.nal.assign(extradata.begin() + static_cast<std::ptrdiff_t>(pos),
                         extradata.begin() + static_cast<std::ptrdiff_t>(pos + size));
        pos += size;
        arrays.push_back(array);
    }
    REQUIRE(pos == extradata.size());
    return arrays;
}

// The first 13 RBSP bytes encode the profile/level fields parsed by the hvcC
// builder: 4+3+1 syntax bits, then profile space/tier/idc, compatibility
// flags, constraint flags and level idc. The trailing bytes are padding so
// the SPS is a realistic size.
Bytes MakeSyntheticHevcSps() {
    Bytes sps = {0x42, 0x01};  // NAL header: type 33 (SPS)
    // No 00 00 00 01 byte run may appear: that pattern would be read as an
    // Annex B start code. Real encoders insert emulation prevention bytes.
    sps.insert(sps.end(), {
        0x01,                                // vps_id=0, max_sub_layers_minus1=0, nesting=1
        0x01,                                // profile_space=0, tier=0, profile_idc=1
        0x60, 0x00, 0x00, 0x00,              // compatibility flags
        0x90, 0x00, 0x00, 0x00, 0x00, 0x00,  // constraint flags
        0x5A,                                // level idc (3.0)
        0x80,                                // trailing bits / padding
    });
    return sps;
}

}  // namespace

TEST_CASE("BuildCodecExtradata builds avcC from H.264 SPS and PPS", "[mux]") {
    const Bytes sps = {0x67, 0x42, 0x00, 0x1E};
    const Bytes pps = {0x68, 0xCE, 0x3C, 0x80};
    const Bytes packet = BuildAnnexB({
        {0x09, 0xF0},  // AUD
        sps,
        pps,
        {0x65, 0x88, 0x84},  // IDR slice
    });

    const Bytes extradata =
        hmmob::BuildCodecExtradata("h264", packet.data(), packet.size());

    REQUIRE(extradata.size() == 19);
    CHECK(extradata[0] == 1);
    CHECK(extradata[1] == 0x42);  // profile_idc
    CHECK(extradata[2] == 0x00);  // constraint flags
    CHECK(extradata[3] == 0x1E);  // level_idc
    CHECK(extradata[4] == 0xFF);  // reserved + lengthSizeMinusOne = 3
    CHECK(extradata[5] == 0xE1);  // reserved + one SPS
    CHECK(extradata[6] == 0x00);
    CHECK(extradata[7] == 0x04);
    CHECK(std::equal(sps.begin(), sps.end(), extradata.begin() + 8));
    CHECK(extradata[12] == 0x01);  // one PPS
    CHECK(extradata[13] == 0x00);
    CHECK(extradata[14] == 0x04);
    CHECK(std::equal(pps.begin(), pps.end(), extradata.begin() + 15));
}

TEST_CASE("BuildCodecExtradata accepts three-byte start codes", "[mux]") {
    const Bytes packet = BuildAnnexB({
        {0x67, 0x4D, 0x40, 0x28},
        {0x68, 0xEE, 0x3C, 0xB0},
    }, false);

    const Bytes extradata =
        hmmob::BuildCodecExtradata("h264", packet.data(), packet.size());

    REQUIRE(extradata.size() == 19);
    CHECK(extradata[1] == 0x4D);
    CHECK(extradata[14] == 0x04);
}

TEST_CASE("BuildCodecExtradata builds hvcC from HEVC VPS, SPS and PPS", "[mux]") {
    const Bytes vps = {0x40, 0x01, 0x0C, 0x01, 0xFF};
    const Bytes sps = MakeSyntheticHevcSps();
    const Bytes pps = {0x44, 0x01, 0xC0};
    const Bytes packet = BuildAnnexB({
        {0x46, 0x01, 0x50},  // AUD (type 35)
        vps,
        sps,
        pps,
        {0x26, 0x01, 0xAF},  // IDR_W_RADL (type 19)
    });

    const Bytes extradata =
        hmmob::BuildCodecExtradata("hevc", packet.data(), packet.size());

    // 23-byte fixed header + three arrays of 5 bytes each + NAL payloads.
    REQUIRE(extradata.size() == 23 + 3 * 5 + vps.size() + sps.size() + pps.size());
    CHECK(extradata[0] == 1);
    CHECK(extradata[1] == 0x01);  // profile_space=0, tier=0, profile_idc=1
    CHECK(extradata[2] == 0x60);  // general_profile_compatibility_flags
    CHECK(extradata[3] == 0x00);
    CHECK(extradata[4] == 0x00);
    CHECK(extradata[5] == 0x00);
    CHECK(extradata[6] == 0x90);  // general_constraint_indicator_flags
    CHECK(extradata[7] == 0x00);
    CHECK(extradata[8] == 0x00);
    CHECK(extradata[9] == 0x00);
    CHECK(extradata[10] == 0x00);
    CHECK(extradata[11] == 0x00);
    CHECK(extradata[12] == 0x5A);  // general_level_idc
    CHECK(extradata[13] == 0xF0);  // reserved + min_spatial_segmentation_idc
    CHECK(extradata[14] == 0x00);
    CHECK(extradata[15] == 0xFC);  // reserved + parallelismType
    CHECK(extradata[16] == 0xFD);  // reserved + chromaFormat = 4:2:0
    CHECK(extradata[17] == 0xF8);  // reserved + bitDepthLumaMinus8
    CHECK(extradata[18] == 0xF8);  // reserved + bitDepthChromaMinus8
    CHECK(extradata[19] == 0x00);  // avgFrameRate
    CHECK(extradata[20] == 0x00);
    CHECK(extradata[21] == 0x0F);  // 1 temporal layer, nested, lengthSize=4
    CHECK(extradata[22] == 3);     // numOfArrays

    const std::vector<HvcArray> arrays = ParseHvcArrays(extradata);
    REQUIRE(arrays.size() == 3);
    CHECK(arrays[0].type == 32);
    CHECK(arrays[0].nal == vps);
    CHECK(arrays[1].type == 33);
    CHECK(arrays[1].nal == sps);
    CHECK(arrays[2].type == 34);
    CHECK(arrays[2].nal == pps);
}

TEST_CASE("BuildCodecExtradata returns empty when parameter sets are missing", "[mux]") {
    // SPS present, PPS missing.
    const Bytes without_pps = BuildAnnexB({
        {0x67, 0x42, 0x00, 0x1E},
        {0x65, 0x88, 0x84},
    });
    CHECK(hmmob::BuildCodecExtradata("h264", without_pps.data(),
                                     without_pps.size())
              .empty());

    // PPS present, SPS missing.
    const Bytes without_sps = BuildAnnexB({
        {0x68, 0xCE, 0x3C, 0x80},
        {0x65, 0x88, 0x84},
    });
    CHECK(hmmob::BuildCodecExtradata("h264", without_sps.data(),
                                     without_sps.size())
              .empty());

    const Bytes hevc_without_vps = BuildAnnexB({
        MakeSyntheticHevcSps(),
        {0x44, 0x01, 0xC0},
    });
    CHECK(hmmob::BuildCodecExtradata("hevc", hevc_without_vps.data(),
                                     hevc_without_vps.size())
              .empty());

    const Bytes unknown_codec = BuildAnnexB({{0x67, 0x42, 0x00, 0x1E}});
    CHECK(hmmob::BuildCodecExtradata("vp9", unknown_codec.data(),
                                     unknown_codec.size())
              .empty());
}

TEST_CASE("BuildCodecExtradata tolerates a truncated HEVC SPS", "[mux]") {
    const Bytes short_sps = {0x42, 0x01, 0x01, 0x01};  // far less than 13 RBSP bytes
    const Bytes packet = BuildAnnexB({
        {0x40, 0x01, 0x0C},
        short_sps,
        {0x44, 0x01, 0xC0},
    });

    CHECK(hmmob::BuildCodecExtradata("hevc", packet.data(), packet.size()).empty());
}
