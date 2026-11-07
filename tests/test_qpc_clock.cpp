#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <thread>
#include "core/common/QpcClock.h"

TEST_CASE("QpcClock returns monotonically increasing microseconds") {
    hmmob::QpcClock clock;
    const auto a = clock.NowUs();
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    const auto b = clock.NowUs();
    REQUIRE(b > a);
    REQUIRE(b - a >= 1000);
}

TEST_CASE("QpcClock never decreases across repeated calls") {
    hmmob::QpcClock clock;
    auto previous = clock.NowUs();
    for (int i = 0; i < 1000; ++i) {
        const auto current = clock.NowUs();
        REQUIRE(current >= previous);
        previous = current;
    }
}
