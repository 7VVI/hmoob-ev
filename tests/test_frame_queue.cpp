#include <catch2/catch_test_macros.hpp>
#include <atomic>
#include <thread>
#include "core/common/FrameQueue.h"

TEST_CASE("FrameQueue pushes and pops in FIFO order") {
    hmmob::FrameQueue<int> q(4);
    REQUIRE(q.Push(1));
    REQUIRE(q.Push(2));
    int value = 0;
    REQUIRE(q.Pop(value, 10));
    REQUIRE(value == 1);
    REQUIRE(q.Pop(value, 10));
    REQUIRE(value == 2);
}

TEST_CASE("FrameQueue rejects when full") {
    hmmob::FrameQueue<int> q(1);
    REQUIRE(q.Push(1));
    REQUIRE_FALSE(q.Push(2));
}

TEST_CASE("FrameQueue Size tracks queued items") {
    hmmob::FrameQueue<int> q(4);
    REQUIRE(q.Size() == 0);
    REQUIRE(q.Push(1));
    REQUIRE(q.Push(2));
    REQUIRE(q.Size() == 2);
    int value = 0;
    REQUIRE(q.Pop(value, 10));
    REQUIRE(q.Size() == 1);
}

TEST_CASE("FrameQueue rejects push after close") {
    hmmob::FrameQueue<int> q(2);
    q.Close();
    REQUIRE_FALSE(q.Push(1));
}

TEST_CASE("FrameQueue close wakes a blocked pop") {
    hmmob::FrameQueue<int> q(1);
    std::atomic<bool> pop_result{true};
    std::thread t([&] {
        int value = 0;
        pop_result = q.Pop(value, 5000);
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    q.Close();
    t.join();
    REQUIRE_FALSE(pop_result.load());
}
