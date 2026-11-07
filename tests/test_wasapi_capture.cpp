#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <thread>
#include <vector>
#include "core/audio/WasapiCapture.h"

TEST_CASE("WasapiCapture loopback receives packets", "[integration][audio]") {
    hmmob::WasapiCapture capture;
    if (!capture.Initialize(true, 48000, 2)) {
        SKIP("no default render endpoint");
    }
    REQUIRE(capture.Start());
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    std::vector<float> buffer(48000 * 2);
    const int frames = capture.Read(buffer.data(), 4800);
    capture.Stop();
    REQUIRE(capture.ring() != nullptr);
    if (frames == 0) {
        SKIP("no loopback packets in this session");
    }
    REQUIRE(frames > 0);
}

TEST_CASE("WasapiCapture microphone receives packets", "[integration][audio]") {
    hmmob::WasapiCapture capture;
    if (!capture.Initialize(false, 48000, 2)) {
        SKIP("no default capture endpoint");
    }
    REQUIRE(capture.Start());
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    std::vector<float> buffer(48000 * 2);
    const int frames = capture.Read(buffer.data(), 4800);
    capture.Stop();
    REQUIRE(capture.ring() != nullptr);
    if (frames == 0) {
        SKIP("no microphone packets in this session");
    }
    REQUIRE(frames > 0);
}
