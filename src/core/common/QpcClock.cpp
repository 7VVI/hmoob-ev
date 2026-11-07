#include "core/common/QpcClock.h"
#include <windows.h>

namespace hmmob {

QpcClock::QpcClock() {
    LARGE_INTEGER frequency{};
    QueryPerformanceFrequency(&frequency);
    frequency_ = frequency.QuadPart;
}

int64_t QpcClock::NowUs() const {
    if (frequency_ <= 0) {
        return 0;
    }
    LARGE_INTEGER counter{};
    QueryPerformanceCounter(&counter);
    const int64_t seconds = counter.QuadPart / frequency_;
    const int64_t remainder = counter.QuadPart % frequency_;
    return seconds * 1'000'000 + (remainder * 1'000'000) / frequency_;
}

}  // namespace hmmob
