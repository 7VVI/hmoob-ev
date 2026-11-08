#pragma once
#include <memory>
#include <string>

#include "core/preview/PreviewFrame.h"

namespace hmmob {

// Streams a downscaled BGRA8 copy of a display to the GUI while no recording
// owns the capture path. Each instance runs its own thread, COM apartment,
// D3D11 device and DXGI desktop duplication; owning a separate device keeps it
// independent from the recorder, which must create its own duplication after
// this one stops.
class DesktopPreview {
public:
    DesktopPreview();
    ~DesktopPreview();
    DesktopPreview(const DesktopPreview&) = delete;
    DesktopPreview& operator=(const DesktopPreview&) = delete;

    // Returns after initialization has finished, so setup errors are
    // synchronous. Stops (and joins) any previous run first.
    [[nodiscard]] bool Start(int display_index, int width, int height, int fps,
                             std::string& error);
    void Stop();
    bool Running() const;
    std::string error() const;
    std::shared_ptr<const PreviewFrame> LatestFrame() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace hmmob
