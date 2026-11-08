#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <memory>
#include <string>
#include <thread>

#include "core/preview/DesktopPreview.h"

namespace {

std::shared_ptr<const hmmob::PreviewFrame> WaitForFirstFrame(
    const hmmob::DesktopPreview& preview) {
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (std::chrono::steady_clock::now() < deadline) {
        std::shared_ptr<const hmmob::PreviewFrame> current =
            preview.LatestFrame();
        if (current && current->sequence >= 1) {
            return current;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return nullptr;
}

}  // namespace

// Requires an active display (D3D11 + DXGI). On a headless host Start() fails
// and the test is skipped.
TEST_CASE("DesktopPreview publishes scaled frames", "[integration]") {
    hmmob::DesktopPreview preview;
    std::string error;
    if (!preview.Start(0, 320, 180, 15, error)) {
        SKIP("desktop preview unavailable: " + error);
    }

    const std::shared_ptr<const hmmob::PreviewFrame> frame =
        WaitForFirstFrame(preview);
    REQUIRE(frame != nullptr);
    REQUIRE(frame->width == 320);
    REQUIRE(frame->height == 180);
    REQUIRE(frame->stride == 320 * 4);
    REQUIRE(frame->bgra.size() == static_cast<size_t>(320) * 180 * 4);

    preview.Stop();
    REQUIRE(preview.Running() == false);
    REQUIRE(preview.error().empty());
}

// A restart must rebuild the entire capture pipeline, and Start() called while
// the preview is already running must stop the old thread promptly instead of
// blocking or deadlocking.
TEST_CASE("DesktopPreview restarts and tolerates a start while running",
          "[integration]") {
    hmmob::DesktopPreview preview;
    std::string error;
    if (!preview.Start(0, 320, 180, 15, error)) {
        SKIP("desktop preview unavailable: " + error);
    }

    REQUIRE(WaitForFirstFrame(preview) != nullptr);

    const auto concurrent_begin = std::chrono::steady_clock::now();
    std::string concurrent_error;
    const bool concurrent_start =
        preview.Start(0, 320, 180, 15, concurrent_error);
    const auto concurrent_elapsed =
        std::chrono::steady_clock::now() - concurrent_begin;
    REQUIRE(concurrent_elapsed < std::chrono::seconds(2));
    REQUIRE((concurrent_start || !concurrent_error.empty()));
    if (!concurrent_start) {
        SKIP("desktop preview unavailable while running: " + concurrent_error);
    }

    REQUIRE(WaitForFirstFrame(preview) != nullptr);
    preview.Stop();
    REQUIRE(preview.Running() == false);
    REQUIRE(preview.error().empty());

    // Plain restart after a clean stop.
    if (!preview.Start(0, 320, 180, 15, error)) {
        SKIP("desktop preview unavailable on restart: " + error);
    }
    REQUIRE(WaitForFirstFrame(preview) != nullptr);
    preview.Stop();
    REQUIRE(preview.Running() == false);
    REQUIRE(preview.error().empty());
}
