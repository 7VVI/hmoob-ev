#include <catch2/catch_test_macros.hpp>
#include <vector>
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
    REQUIRE(out[0] > 0.39f);
    REQUIRE(out[0] < 0.41f);
    REQUIRE(out[2] > 0.39f);
    REQUIRE(out[2] < 0.41f);
}

TEST_CASE("AudioMixer clamps to [-1, 1]") {
    hmmob::PcmRingBuffer system(1, 8);
    const float loud[] = {9.0f, 9.0f};
    system.Write(loud, 2);

    hmmob::AudioMixer mixer(48000, 1);
    mixer.SetSystemSource(&system);
    float out[2] = {};
    mixer.Mix(out, 2);
    REQUIRE(out[0] == 1.0f);
    REQUIRE(out[1] == 1.0f);
}

TEST_CASE("AudioMixer treats missing sources as silence") {
    hmmob::AudioMixer mixer(48000, 2);
    float out[4] = {1, 1, 1, 1};
    mixer.Mix(out, 2);
    REQUIRE(out[0] == 0.0f);
    REQUIRE(out[3] == 0.0f);
}
