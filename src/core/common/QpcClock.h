#pragma once
#include <cstdint>

namespace hmmob {

class QpcClock {
public:
    QpcClock();
    int64_t NowUs() const;

private:
    int64_t frequency_ = 0;
};

}  // namespace hmmob
