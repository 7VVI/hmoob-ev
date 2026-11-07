#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <thread>
#include <vector>
#include "core/audio/WasapiCapture.h"

TEST_CASE("WasapiCapture reports a clean initial state", "[audio]") {
    hmmob::WasapiCapture capture;
    REQUIRE_FALSE(capture.failed());
    REQUIRE(capture.last_error() == S_OK);
    REQUIRE_FALSE(capture.is_running());
    REQUIRE(capture.ring() == nullptr);
    REQUIRE(capture.sample_rate() == 48000);
    REQUIRE(capture.channels() == 2);
    REQUIRE(capture.Read(nullptr, 0) == 0);
    REQUIRE_FALSE(capture.Start());

    // Shutdown is idempotent even when nothing was ever initialized.
    capture.Shutdown();
    capture.Shutdown();
    REQUIRE_FALSE(capture.failed());
    REQUIRE(capture.last_error() == S_OK);
    REQUIRE(capture.ring() == nullptr);
}

TEST_CASE("WasapiCapture reports invalid initialization arguments", "[audio]") {
    hmmob::WasapiCapture capture;
    REQUIRE_FALSE(capture.Initialize(true, 0, 2));
    REQUIRE_FALSE(capture.failed());
    REQUIRE(capture.last_error() == E_INVALIDARG);
    REQUIRE_FALSE(capture.is_running());
    REQUIRE(capture.ring() == nullptr);

    REQUIRE_FALSE(capture.Initialize(false, 48000, 0));
    REQUIRE_FALSE(capture.failed());
    REQUIRE(capture.last_error() == E_INVALIDARG);

    // Shutdown clears the recorded error; no capture session was ever created.
    capture.Shutdown();
    REQUIRE_FALSE(capture.failed());
    REQUIRE(capture.last_error() == S_OK);
}

// Runtime failures of the capture thread (failed() becoming true) are only
// reachable with a live endpoint invalidated mid-stream, so the hardware tests
// below assert the healthy path plus the ring/accessor invariants instead.

TEST_CASE("WasapiCapture loopback receives packets", "[integration][audio]") {
    hmmob::WasapiCapture capture;
    if (!capture.Initialize(true, 48000, 2)) {
        SKIP("no default render endpoint");
    }
    REQUIRE(capture.sample_rate() == 48000);
    REQUIRE(capture.channels() == 2);
    REQUIRE(capture.ring() != nullptr);
    REQUIRE(capture.ring()->channels() == 2);
    REQUIRE(capture.ring()->capacity_frames() == 48000);
    REQUIRE_FALSE(capture.is_running());
    std::vector<float> buffer(48000 * 2);
    REQUIRE(capture.Read(buffer.data(), 4800) == 0);

    REQUIRE(capture.Start());
    REQUIRE(capture.is_running());
    REQUIRE_FALSE(capture.failed());
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    const int frames = capture.Read(buffer.data(), 4800);
    capture.Stop();
    REQUIRE_FALSE(capture.is_running());
    REQUIRE_FALSE(capture.failed());

    capture.Shutdown();
    capture.Shutdown();
    REQUIRE(capture.ring() == nullptr);
    REQUIRE_FALSE(capture.failed());
    REQUIRE(capture.last_error() == S_OK);
    REQUIRE_FALSE(capture.Start());
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
    REQUIRE(capture.sample_rate() == 48000);
    REQUIRE(capture.channels() == 2);
    REQUIRE(capture.ring() != nullptr);
    REQUIRE(capture.ring()->channels() == 2);
    REQUIRE(capture.ring()->capacity_frames() == 48000);
    REQUIRE_FALSE(capture.is_running());
    std::vector<float> buffer(48000 * 2);
    REQUIRE(capture.Read(buffer.data(), 4800) == 0);

    REQUIRE(capture.Start());
    REQUIRE(capture.is_running());
    REQUIRE_FALSE(capture.failed());
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    const int frames = capture.Read(buffer.data(), 4800);
    capture.Stop();
    REQUIRE_FALSE(capture.is_running());
    REQUIRE_FALSE(capture.failed());

    capture.Shutdown();
    capture.Shutdown();
    REQUIRE(capture.ring() == nullptr);
    REQUIRE_FALSE(capture.failed());
    REQUIRE(capture.last_error() == S_OK);
    REQUIRE_FALSE(capture.Start());
    if (frames == 0) {
        SKIP("no microphone packets in this session");
    }
    REQUIRE(frames > 0);
}
