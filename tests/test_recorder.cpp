#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>

#include "core/recorder/Recorder.h"

// Requires an active display (D3D11 + DXGI + AMF). On a headless host Start()
// fails and the test is skipped.
TEST_CASE("Recorder treats an immediate stop as a clean recording",
          "[integration]") {
    const std::filesystem::path output =
        std::filesystem::temp_directory_path() /
        "hmmob-recorder-stop-test.mp4";
    std::error_code ec;
    std::filesystem::remove(output, ec);

    hmmob::Recorder recorder;
    hmmob::RecorderConfig config;
    config.output_path = output.string();
    config.duration_seconds = 0;
    config.fps = 60;
    config.codec = "hevc";

    std::string error;
    const bool ok = recorder.Start(config, error);
    if (!ok) {
        SKIP("recorder unavailable: " + error);
    }

    recorder.RequestStop();
    recorder.Stop();

    REQUIRE(recorder.Finished());
    REQUIRE(recorder.error().empty());

    std::filesystem::remove(output, ec);
}

// Requires an active display (D3D11 + DXGI + AMF), like the test above.
TEST_CASE("Recorder publishes preview frames", "[integration]") {
    const std::filesystem::path output =
        std::filesystem::temp_directory_path() /
        "hmmob-recorder-preview-test.mp4";
    std::error_code ec;
    std::filesystem::remove(output, ec);

    hmmob::Recorder recorder;
    hmmob::RecorderConfig config;
    config.output_path = output.string();
    config.duration_seconds = 0;
    config.fps = 60;
    config.codec = "hevc";

    std::string error;
    if (!recorder.Start(config, error)) {
        SKIP("recorder unavailable: " + error);
    }

    // Wait for at least three published previews. A successful Start with no
    // preview frames within the timeout is a failure, not a skip.
    std::shared_ptr<const hmmob::PreviewFrame> frame;
    uint64_t last_sequence = 0;
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (std::chrono::steady_clock::now() < deadline) {
        std::shared_ptr<const hmmob::PreviewFrame> current =
            recorder.LatestPreview();
        if (current) {
            // Sequence numbers only ever increase across polls.
            REQUIRE(current->sequence >= last_sequence);
            if (current->sequence > last_sequence) {
                last_sequence = current->sequence;
                frame = std::move(current);
            }
            if (last_sequence >= 3) {
                break;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    REQUIRE(frame != nullptr);
    REQUIRE(last_sequence >= 3);
    REQUIRE(frame->width == 640);
    REQUIRE(frame->height == 360);
    REQUIRE(frame->stride == frame->width * 4);
    REQUIRE(frame->bgra.size() ==
            static_cast<size_t>(frame->width) * frame->height * 4);

    // A solid-color desktop produces a legitimate uniform buffer, so this is
    // only a hint that the readback copied real pixels.
    const bool uniform =
        std::all_of(frame->bgra.begin(), frame->bgra.end(),
                    [&](uint8_t byte) { return byte == frame->bgra.front(); });
    if (uniform) {
        WARN("preview buffer is a single uniform color");
    }

    recorder.RequestStop();
    recorder.Stop();

    REQUIRE(recorder.Finished());
    REQUIRE(recorder.error().empty());

    std::filesystem::remove(output, ec);
}
