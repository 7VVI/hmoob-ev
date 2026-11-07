#include <catch2/catch_test_macros.hpp>
#include <stdexcept>
#include "core/audio/PcmRingBuffer.h"

TEST_CASE("PcmRingBuffer writes and reads interleaved stereo") {
    hmmob::PcmRingBuffer ring(2, 8);
    const float input[] = {1, 2, 3, 4, 5, 6};
    ring.Write(input, 3);
    REQUIRE(ring.available_frames() == 3);

    float output[6] = {};
    REQUIRE(ring.Read(output, 3) == 3);
    REQUIRE(output[0] == 1);
    REQUIRE(output[5] == 6);
    REQUIRE(ring.available_frames() == 0);
}

TEST_CASE("PcmRingBuffer drops oldest frames on overflow") {
    hmmob::PcmRingBuffer ring(1, 4);
    const float first[] = {1, 2, 3, 4};
    ring.Write(first, 4);
    const float extra[] = {5, 6};
    ring.Write(extra, 2);

    float output[4] = {};
    REQUIRE(ring.Read(output, 4) == 4);
    REQUIRE(output[0] == 3);
    REQUIRE(output[3] == 6);
}

TEST_CASE("PcmRingBuffer underflow returns available frames") {
    hmmob::PcmRingBuffer ring(1, 8);
    const float input[] = {1, 2};
    ring.Write(input, 2);

    float output[4] = {};
    REQUIRE(ring.Read(output, 4) == 2);
    REQUIRE(output[0] == 1);
    REQUIRE(output[1] == 2);
    REQUIRE(output[2] == 0);
}

TEST_CASE("PcmRingBuffer holds exactly capacity frames without dropping") {
    hmmob::PcmRingBuffer ring(1, 4);
    const float input[] = {1, 2, 3, 4};
    ring.Write(input, 4);
    REQUIRE(ring.available_frames() == 4);

    float output[4] = {};
    REQUIRE(ring.Read(output, 4) == 4);
    REQUIRE(output[0] == 1);
    REQUIRE(output[3] == 4);
    REQUIRE(ring.available_frames() == 0);
}

TEST_CASE("PcmRingBuffer survives multiple wraps") {
    hmmob::PcmRingBuffer ring(2, 4);
    const float input[] = {1, 2, 3, 4, 5, 6};
    for (int round = 0; round < 5; ++round) {
        ring.Write(input, 3);
        REQUIRE(ring.available_frames() == 3);

        float output[6] = {};
        REQUIRE(ring.Read(output, 3) == 3);
        REQUIRE(output[0] == 1);
        REQUIRE(output[1] == 2);
        REQUIRE(output[4] == 5);
        REQUIRE(output[5] == 6);
        REQUIRE(ring.available_frames() == 0);
    }
}

TEST_CASE("PcmRingBuffer rejects invalid dimensions") {
    REQUIRE_THROWS_AS(hmmob::PcmRingBuffer(0, 8), std::invalid_argument);
    REQUIRE_THROWS_AS(hmmob::PcmRingBuffer(-1, 8), std::invalid_argument);
    REQUIRE_THROWS_AS(hmmob::PcmRingBuffer(2, 0), std::invalid_argument);
    REQUIRE_THROWS_AS(hmmob::PcmRingBuffer(2, -4), std::invalid_argument);
}
