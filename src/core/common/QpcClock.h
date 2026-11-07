#pragma once
#include <cstdint>

namespace hmmob {

class QpcClock {
public:
    QpcClock();
    static int64_t TicksToUs(int64_t ticks, int64_t frequency);
    int64_t TicksToUs(int64_t ticks) const;
    int64_t NowUs() const;

private:
    int64_t frequency_ = 0;
};

}  // namespace hmmob
