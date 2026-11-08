#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <string>

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
