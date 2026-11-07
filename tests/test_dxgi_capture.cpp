#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <windows.h>
#include "core/capture/DxgiCapture.h"

TEST_CASE("DXGI capture starts and resets without a failure") {
    hmmob::DxgiCapture capture;
    REQUIRE_FALSE(capture.failed());
    REQUIRE(capture.last_error() == S_OK);
    capture.Shutdown();
    REQUIRE_FALSE(capture.failed());
    REQUIRE(capture.last_error() == S_OK);
}

TEST_CASE("DXGI capture reports failure for an invalid display index", "[integration]") {
    hmmob::D3D11Device d3d;
    if (!d3d.Create()) {
        SKIP("D3D11 device unavailable");
    }

    hmmob::DxgiCapture capture;
    REQUIRE_FALSE(capture.Initialize(d3d, -1, false));
    REQUIRE(capture.failed());
    REQUIRE(capture.last_error() != S_OK);
}

TEST_CASE("DXGI capture acquires frames from the primary display", "[integration]") {
    // Desktop Duplication produces no frames when the session's input desktop
    // is locked (for example, after an overnight lock screen).
    HDESK desktop = OpenInputDesktop(0, FALSE, DESKTOP_READOBJECTS);
    if (!desktop) {
        SKIP("input desktop is locked");
    }
    CloseDesktop(desktop);

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
    if (!GetCursorPos(&original)) {
        SKIP("cannot move cursor in this session");
    }
    struct CursorRestore {
        POINT p;
        ~CursorRestore() { SetCursorPos(p.x, p.y); }
    } restore{original};

    int frames = 0;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    for (int i = 0; frames < 20 && std::chrono::steady_clock::now() < deadline; ++i) {
        // Desktop Duplication only produces a frame when the desktop changes.
        // Nudge the cursor so a static desktop still generates frames.
        if (!SetCursorPos(original.x + (i % 2 ? 1 : -1), original.y)) {
            SKIP("cannot move cursor in this session");
        }
        hmmob::CaptureFrame frame;
        if (capture.Acquire(frame, 250)) {
            REQUIRE(frame.texture);
            REQUIRE(frame.width > 0);
            REQUIRE(frame.height > 0);
            ++frames;
        }
    }
    REQUIRE(frames > 0);
}
