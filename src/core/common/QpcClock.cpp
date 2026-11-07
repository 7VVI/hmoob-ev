#include "core/common/QpcClock.h"
#include <windows.h>

namespace hmmob {

QpcClock::QpcClock() {
    LARGE_INTEGER frequency{};
    QueryPerformanceFrequency(&frequency);
    frequency_ = frequency.QuadPart;
}

int64_t QpcClock::NowUs() const {
    LARGE_INTEGER counter{};
    QueryPerformanceCounter(&counter);
    return (counter.QuadPart * 1'000'000) / frequency_;
}

}  // namespace hmmob
