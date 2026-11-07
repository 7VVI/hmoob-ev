#include <catch2/catch_test_macros.hpp>
#include <stdexcept>
#include "core/audio/AudioMixer.h"
#include "core/audio/PcmRingBuffer.h"

TEST_CASE("AudioMixer sums two sources with gains") {
    hmmob::PcmRingBuffer system(2, 16);
    hmmob::PcmRingBuffer mic(2, 16);
    const float a[] = {0.2f, 0.2f, 0.2f, 0.2f};
    const float b[] = {0.1f, 0.1f, 0.1f, 0.1f};
    system.Write(a, 2);
    mic.Write(b, 2);

    hmmob::AudioMixer mixer(48000, 2);
    mixer.SetSystemSource(&system);
    mixer.SetMicSource(&mic);
    mixer.SetSystemGain(1.0f);
    mixer.SetMicGain(2.0f);

    float out[4] = {};
    mixer.Mix(out, 2);
    // Raw sum is 0.2 + 2 * 0.1 = 0.4; the tanh soft limiter maps it to ~0.37995.
    REQUIRE(out[0] > 0.379f);
    REQUIRE(out[0] < 0.381f);
    REQUIRE(out[2] > 0.379f);
    REQUIRE(out[2] < 0.381f);
}

TEST_CASE("AudioMixer soft-limits large input without hard clipping") {
    hmmob::PcmRingBuffer system(1, 8);
    const float loud[] = {9.0f, -9.0f};
    system.Write(loud, 2);

    hmmob::AudioMixer mixer(48000, 1);
    mixer.SetSystemSource(&system);
    float out[2] = {};
    mixer.Mix(out, 2);
    REQUIRE(out[0] > 0.99f);
    REQUIRE(out[0] < 1.0f);
    REQUIRE(out[1] < -0.99f);
    REQUIRE(out[1] > -1.0f);
}

TEST_CASE("AudioMixer treats missing sources as silence") {
    hmmob::AudioMixer mixer(48000, 2);
    float out[4] = {1, 1, 1, 1};
    mixer.Mix(out, 2);
    REQUIRE(out[0] == 0.0f);
    REQUIRE(out[3] == 0.0f);
}

TEST_CASE("AudioMixer mutes a source but still drains it") {
    hmmob::PcmRingBuffer system(1, 8);
    const float input[] = {0.5f, 0.5f};
    system.Write(input, 2);

    hmmob::AudioMixer mixer(48000, 1);
    mixer.SetSystemSource(&system);
    mixer.SetSystemMuted(true);

    float out[2] = {1, 1};
    mixer.Mix(out, 2);
    REQUIRE(out[0] == 0.0f);
    REQUIRE(out[1] == 0.0f);
    REQUIRE(system.available_frames() == 0);
}

TEST_CASE("AudioMixer ignores a source with mismatched channel count") {
    hmmob::PcmRingBuffer mono(1, 8);
    const float input[] = {0.5f, 0.5f};
    mono.Write(input, 2);

    hmmob::AudioMixer mixer(48000, 2);
    mixer.SetSystemSource(&mono);

    float out[4] = {-1, -1, -1, -1};
    mixer.Mix(out, 2);
    REQUIRE(out[0] == 0.0f);
    REQUIRE(out[1] == 0.0f);
    REQUIRE(out[2] == 0.0f);
    REQUIRE(out[3] == 0.0f);
    // A mismatched source is ignored, not read, so its data stays queued.
    REQUIRE(mono.available_frames() == 2);
}

TEST_CASE("AudioMixer rejects invalid configuration") {
    REQUIRE_THROWS_AS(hmmob::AudioMixer(0, 2), std::invalid_argument);
    REQUIRE_THROWS_AS(hmmob::AudioMixer(-48000, 2), std::invalid_argument);
    REQUIRE_THROWS_AS(hmmob::AudioMixer(48000, 0), std::invalid_argument);
    REQUIRE_THROWS_AS(hmmob::AudioMixer(48000, -2), std::invalid_argument);
}
