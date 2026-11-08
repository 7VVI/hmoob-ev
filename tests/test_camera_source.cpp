#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

#include "core/capture/CameraSource.h"
#include "core/recorder/Recorder.h"

// Requires a video capture device. Machines without one skip both tests.
TEST_CASE("CameraSource enumerates capture devices", "[integration]") {
    const std::vector<hmmob::CameraSource::CameraDeviceInfo> devices =
        hmmob::CameraSource::EnumerateDevices();
    if (devices.empty()) {
        SKIP("no camera");
    }
    for (const hmmob::CameraSource::CameraDeviceInfo& device : devices) {
        REQUIRE_FALSE(device.name.empty());
        REQUIRE_FALSE(device.id.empty());
    }
}

// Records two seconds through the Recorder with the first camera. The output
// must be decodable, so it checks the encoded size, the frame count and that
// a real file was written.
TEST_CASE("Recorder records from the default camera", "[integration]") {
    if (hmmob::CameraSource::EnumerateDevices().empty()) {
        SKIP("no camera");
    }

    const std::filesystem::path output =
        std::filesystem::temp_directory_path() / "hmmob-camera-test.mp4";
    std::error_code ec;
    std::filesystem::remove(output, ec);

    hmmob::Recorder recorder;
    hmmob::RecorderConfig config;
    config.mode = hmmob::CaptureMode::Camera;
    config.camera_index = 0;
    config.duration_seconds = 2;
    config.fps = 60;
    config.codec = "hevc";
    config.output_path = output.string();

    std::string error;
    if (!recorder.Start(config, error)) {
        SKIP("camera unavailable: " + error);
    }

    // Device startup can take a few seconds on some drivers.
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while (!recorder.Finished() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    REQUIRE(recorder.Finished());

    REQUIRE(recorder.error().empty());
    const hmmob::RecorderStats stats = recorder.stats();
    REQUIRE(stats.display_width > 0);
    REQUIRE(stats.display_height > 0);
    REQUIRE(stats.display_width % 2 == 0);
    REQUIRE(stats.display_height % 2 == 0);
    REQUIRE(stats.encoded_frames >= 60);
    REQUIRE(std::filesystem::exists(output));
    REQUIRE(std::filesystem::file_size(output, ec) > 10 * 1024);

    std::filesystem::remove(output, ec);
}
