#include "core/common/QpcClock.h"
#include <windows.h>

namespace hmmob {

QpcClock::QpcClock() {
    LARGE_INTEGER frequency{};
    QueryPerformanceFrequency(&frequency);
    frequency_ = frequency.QuadPart;
}

int64_t QpcClock::TicksToUs(int64_t ticks, int64_t frequency) {
    if (frequency <= 0) {
        return 0;
    }
    const int64_t seconds = ticks / frequency;
    const int64_t remainder = ticks % frequency;
    return seconds * 1'000'000 + (remainder * 1'000'000) / frequency;
}

int64_t QpcClock::TicksToUs(int64_t ticks) const {
    return TicksToUs(ticks, frequency_);
}

int64_t QpcClock::NowUs() const {
    if (frequency_ <= 0) {
        return 0;
    }
    LARGE_INTEGER counter{};
    QueryPerformanceCounter(&counter);
    return TicksToUs(counter.QuadPart, frequency_);
}

}  // namespace hmmob
