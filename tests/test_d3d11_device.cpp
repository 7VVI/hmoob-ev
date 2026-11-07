#include <catch2/catch_test_macros.hpp>
#include "core/gfx/D3D11Device.h"

TEST_CASE("D3D11 device creates and enumerates at least one display") {
    hmmob::D3D11Device d3d;
    REQUIRE(d3d.Create());
    const auto displays = d3d.EnumerateDisplays();
    REQUIRE_FALSE(displays.empty());
    for (const auto& d : displays) {
        REQUIRE(d.width > 0);
        REQUIRE(d.height > 0);
    }
}
