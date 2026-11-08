#pragma once
#include <cstdint>
#include <vector>

namespace hmmob {

// One BGRA8 frame scaled for the GUI, shared between the recorder's preview
// pipeline and the idle desktop preview. Immutable once published.
struct PreviewFrame {
    int width = 0;
    int height = 0;
    int stride = 0;
    std::vector<uint8_t> bgra;
    uint64_t sequence = 0;
};

}  // namespace hmmob
