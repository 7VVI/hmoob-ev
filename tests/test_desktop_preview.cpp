#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <memory>
#include <string>
#include <thread>

#include "core/preview/DesktopPreview.h"

// Requires an active display (D3D11 + DXGI). On a headless host Start() fails
// and the test is skipped.
TEST_CASE("DesktopPreview publishes scaled frames", "[integration]") {
    hmmob::DesktopPreview preview;
    std::string error;
    if (!preview.Start(0, 320, 180, 15, error)) {
        SKIP("desktop preview unavailable: " + error);
    }

    std::shared_ptr<const hmmob::PreviewFrame> frame;
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (std::chrono::steady_clock::now() < deadline) {
        std::shared_ptr<const hmmob::PreviewFrame> current =
            preview.LatestFrame();
        if (current && current->sequence >= 1) {
            frame = std::move(current);
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    REQUIRE(frame != nullptr);
    REQUIRE(frame->width == 320);
    REQUIRE(frame->height == 180);
    REQUIRE(frame->stride == 320 * 4);
    REQUIRE(frame->bgra.size() == static_cast<size_t>(320) * 180 * 4);

    preview.Stop();
    REQUIRE(preview.Running() == false);
    REQUIRE(preview.error().empty());
}
