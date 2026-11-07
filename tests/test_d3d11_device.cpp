#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <cwchar>
#include <vector>
#include "core/gfx/D3D11Device.h"

TEST_CASE("D3D11 device creates and enumerates at least one display", "[integration]") {
    hmmob::D3D11Device d3d;
    REQUIRE(d3d.Create());
    REQUIRE(d3d.feature_level() >= D3D_FEATURE_LEVEL_11_0);

    const auto displays = d3d.EnumerateDisplays();
    REQUIRE_FALSE(displays.empty());

    std::vector<int> indices;
    indices.reserve(displays.size());
    for (const auto& d : displays) {
        REQUIRE(d.width > 0);
        REQUIRE(d.height > 0);
        REQUIRE(d.adapter_index >= 0);
        REQUIRE(d.output_index >= 0);
        REQUIRE(d.attached_to_desktop);
        REQUIRE_FALSE(d.name.empty());
        indices.push_back(d.index);
    }

    std::sort(indices.begin(), indices.end());
    REQUIRE(std::adjacent_find(indices.begin(), indices.end()) == indices.end());
}

TEST_CASE("IsHdrColorSpace recognizes HDR and SDR color spaces") {
    REQUIRE(hmmob::IsHdrColorSpace(DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020));
    REQUIRE(hmmob::IsHdrColorSpace(DXGI_COLOR_SPACE_RGB_STUDIO_G2084_NONE_P2020));
    REQUIRE(hmmob::IsHdrColorSpace(DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709));
    REQUIRE_FALSE(hmmob::IsHdrColorSpace(DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709));
}

TEST_CASE("FromDesc1 maps descriptor fields") {
    DXGI_OUTPUT_DESC1 desc{};
    wcscpy_s(desc.DeviceName, L"\\\\.\\DISPLAY1");
    desc.DesktopCoordinates = RECT{0, 0, 1920, 1080};
    desc.AttachedToDesktop = TRUE;
    desc.Rotation = DXGI_MODE_ROTATION_IDENTITY;
    desc.ColorSpace = DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020;
    desc.MaxLuminance = 1000.0f;
    desc.MaxFullFrameLuminance = 600.0f;
    desc.BitsPerColor = 10;

    const auto info = hmmob::FromDesc1(desc, 7, 2, 1);
    REQUIRE(info.index == 7);
    REQUIRE(info.adapter_index == 2);
    REQUIRE(info.output_index == 1);
    REQUIRE(info.name == L"\\\\.\\DISPLAY1");
    REQUIRE(info.width == 1920);
    REQUIRE(info.height == 1080);
    REQUIRE(info.attached_to_desktop);
    REQUIRE(info.hdr);
    REQUIRE(info.rotation == DXGI_MODE_ROTATION_IDENTITY);
    REQUIRE(info.max_luminance == 1000.0f);
    REQUIRE(info.max_full_frame_luminance == 600.0f);
    REQUIRE(info.bits_per_color == 10);
}

TEST_CASE("FromDesc1 treats SDR color space as non-HDR") {
    DXGI_OUTPUT_DESC1 desc{};
    wcscpy_s(desc.DeviceName, L"\\\\.\\DISPLAY2");
    desc.DesktopCoordinates = RECT{0, 0, 2560, 1440};
    desc.AttachedToDesktop = TRUE;
    desc.Rotation = DXGI_MODE_ROTATION_ROTATE90;
    desc.ColorSpace = DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
    desc.BitsPerColor = 8;

    const auto info = hmmob::FromDesc1(desc, 0, 1, 0);
    REQUIRE_FALSE(info.hdr);
    REQUIRE(info.color_space == DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709);
    REQUIRE(info.rotation == DXGI_MODE_ROTATION_ROTATE90);
    REQUIRE(info.bits_per_color == 8);
}
