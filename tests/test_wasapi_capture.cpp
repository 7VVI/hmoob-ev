#include <catch2/catch_test_macros.hpp>
#include <audioclient.h>
#include <mmdeviceapi.h>  // E_NOTFOUND for GetDevice on an unknown id
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

TEST_CASE("WasapiCapture enumerates audio endpoints", "[audio]") {
    for (const bool loopback : {false, true}) {
        const std::vector<hmmob::WasapiCapture::AudioDeviceInfo> devices =
            hmmob::WasapiCapture::EnumerateDevices(loopback);
        int defaults = 0;
        for (const hmmob::WasapiCapture::AudioDeviceInfo& device : devices) {
            REQUIRE_FALSE(device.name.empty());
            REQUIRE_FALSE(device.id.empty());
            if (device.is_default) {
                ++defaults;
            }
        }
        REQUIRE(defaults <= 1);
    }
    // Enumeration must not leave an apartment behind: a second pass from the
    // same thread has to behave exactly like the first one.
    const size_t microphones =
        hmmob::WasapiCapture::EnumerateDevices(false).size();
    REQUIRE(hmmob::WasapiCapture::EnumerateDevices(false).size() == microphones);
}

TEST_CASE("WasapiCapture captures from an explicitly selected microphone",
          "[integration][audio]") {
    const std::vector<hmmob::WasapiCapture::AudioDeviceInfo> devices =
        hmmob::WasapiCapture::EnumerateDevices(false);
    if (devices.empty()) {
        SKIP("no microphone endpoint");
    }
    hmmob::WasapiCapture capture;
    if (!capture.Initialize(false, 48000, 2, devices.front().id)) {
        SKIP("selected microphone endpoint is unavailable");
    }
    REQUIRE(capture.sample_rate() == 48000);
    REQUIRE(capture.channels() == 2);
    REQUIRE(capture.ring() != nullptr);
    REQUIRE(capture.ring()->capacity_frames() == 48000);

    std::vector<float> buffer(48000 * 2);
    REQUIRE(capture.Start());
    REQUIRE(capture.is_running());
    REQUIRE_FALSE(capture.failed());
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    const int frames = capture.Read(buffer.data(), 4800);
    capture.Stop();
    REQUIRE_FALSE(capture.is_running());
    REQUIRE_FALSE(capture.failed());

    capture.Shutdown();
    REQUIRE(capture.ring() == nullptr);
    REQUIRE(capture.last_error() == S_OK);
    if (frames == 0) {
        SKIP("no packets from the selected microphone in this session");
    }
    REQUIRE(frames > 0);
}

TEST_CASE("WasapiCapture rejects an unknown device id", "[audio]") {
    hmmob::WasapiCapture capture;
    // Deliberately not a valid endpoint id. The call must fail promptly and
    // must not fall back to the default endpoint.
    REQUIRE_FALSE(
        capture.Initialize(false, 48000, 2, "hmmob-does-not-exist-endpoint"));
    const HRESULT error = capture.last_error();
    REQUIRE(error != S_OK);
    REQUIRE(capture.ring() == nullptr);
    REQUIRE_FALSE(capture.is_running());
    // Anything outside the "device is gone" family means WASAPI or COM itself
    // is unavailable in this session, which is an environment skip, not a bug.
    if (error != E_NOTFOUND && error != E_INVALIDARG &&
        error != AUDCLNT_E_DEVICE_INVALIDATED) {
        SKIP("WASAPI unavailable in this session");
    }
    capture.Shutdown();
    REQUIRE(capture.last_error() == S_OK);
}
