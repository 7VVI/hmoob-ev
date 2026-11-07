#pragma once
#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <utility>

namespace hmmob {

// Owner contract: call Close() and join all consumers before destroying the queue.
template <typename T>
class FrameQueue {
public:
    explicit FrameQueue(size_t capacity) : capacity_(capacity) {}

    ~FrameQueue() {
        assert(waiters_.load(std::memory_order_relaxed) == 0 &&
               "FrameQueue destroyed with a blocked consumer; call Close() and join first");
    }

    bool Push(T item) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (closed_ || queue_.size() >= capacity_) {
            return false;
        }
        queue_.push_back(std::move(item));
        cv_.notify_one();
        return true;
    }

    bool Pop(T& out, int timeout_ms) {
        std::unique_lock<std::mutex> lock(mutex_);
        waiters_.fetch_add(1, std::memory_order_relaxed);
        const bool notified = cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms),
                                           [this] { return closed_ || !queue_.empty(); });
        waiters_.fetch_sub(1, std::memory_order_relaxed);
        if (!notified) {
            return false;
        }
        if (queue_.empty()) {
            return false;
        }
        out = std::move(queue_.front());
        queue_.pop_front();
        return true;
    }

    void Close() {
        std::lock_guard<std::mutex> lock(mutex_);
        closed_ = true;
        cv_.notify_all();
    }

    size_t Size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.size();
    }

    int waiter_count() const { return waiters_.load(std::memory_order_relaxed); }

private:
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<T> queue_;
    size_t capacity_;
    bool closed_ = false;
    std::atomic<int> waiters_{0};
};

}  // namespace hmmob
