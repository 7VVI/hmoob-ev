#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <windows.h>
#include "core/capture/DxgiCapture.h"

TEST_CASE("DXGI capture acquires frames from the primary display", "[integration]") {
    hmmob::D3D11Device d3d;
    if (!d3d.Create()) {
        SKIP("D3D11 device unavailable");
    }
    if (d3d.EnumerateDisplays().empty()) {
        SKIP("no display available");
    }

    hmmob::DxgiCapture capture;
    if (!capture.Initialize(d3d, 0, false)) {
        SKIP("desktop duplication unavailable");
    }

    POINT original{};
    GetCursorPos(&original);

    int frames = 0;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    for (int i = 0; frames < 20 && std::chrono::steady_clock::now() < deadline; ++i) {
        // Desktop Duplication only produces a frame when the desktop changes.
        // Nudge the cursor so a static desktop still generates frames.
        SetCursorPos(original.x + (i % 2 ? 1 : -1), original.y);
        hmmob::CaptureFrame frame;
        if (capture.Acquire(frame, 250)) {
            REQUIRE(frame.texture);
            REQUIRE(frame.width > 0);
            REQUIRE(frame.height > 0);
            ++frames;
        }
    }
    SetCursorPos(original.x, original.y);
    REQUIRE(frames > 0);
}
