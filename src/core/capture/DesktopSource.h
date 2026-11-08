#pragma once
#include "core/capture/DxgiCapture.h"
#include "core/capture/VideoSource.h"

namespace hmmob {

// Desktop Duplication backed source. Screen mode delivers the whole display;
// Region mode delivers the full display texture plus the encoded sub-rectangle.
class DesktopSource final : public VideoSource {
public:
    bool Initialize(D3D11Device& d3d, const RecorderConfig& cfg,
                    std::string& error) override;
    bool Acquire(SourceFrame& frame, int timeout_ms) override;

    int width() const override { return width_; }
    int height() const override { return height_; }
    int texture_width() const override { return capture_.width(); }
    int texture_height() const override { return capture_.height(); }
    DXGI_FORMAT format() const override { return format_; }
    DXGI_COLOR_SPACE_TYPE color_space() const override {
        return capture_.color_space();
    }
    bool hdr_capable() const override { return capture_.hdr(); }
    bool hdr_downgraded() const override { return capture_.hdr_downgraded(); }
    bool failed() const override { return capture_.failed(); }
    bool access_lost() const override { return capture_.access_lost(); }
    HRESULT last_error() const override { return capture_.last_error(); }

private:
    DxgiCapture capture_;
    int width_ = 0;
    int height_ = 0;
    DXGI_FORMAT format_ = DXGI_FORMAT_UNKNOWN;
    RECT source_rect_{};
};

}  // namespace hmmob
