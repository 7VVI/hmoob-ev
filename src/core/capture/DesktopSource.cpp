#include "core/capture/DesktopSource.h"

#include <cstdio>

namespace hmmob {

namespace {

bool RectInside(int x, int y, int width, int height, int bounds_width,
                int bounds_height) {
    // int64 arithmetic keeps the right/bottom edge from overflowing.
    return x >= 0 && y >= 0 && width > 0 && height > 0 &&
           static_cast<int64_t>(x) + width <= bounds_width &&
           static_cast<int64_t>(y) + height <= bounds_height;
}

}  // namespace

bool DesktopSource::Initialize(D3D11Device& d3d, const RecorderConfig& cfg,
                               std::string& error) {
    if (!capture_.Initialize(d3d, cfg.display_index, cfg.hdr)) {
        error = "failed to initialize dxgi capture";
        return false;
    }

    format_ = capture_.format();
    if (cfg.mode == CaptureMode::Region) {
        if (cfg.region_width <= 0 || cfg.region_height <= 0) {
            error = "invalid region: width and height must be positive";
            return false;
        }
        if (!RectInside(cfg.region_x, cfg.region_y, cfg.region_width,
                        cfg.region_height, capture_.width(),
                        capture_.height())) {
            char message[160];
            std::snprintf(message, sizeof(message),
                          "invalid region: %dx%d at (%d,%d) is outside the "
                          "%dx%d display",
                          cfg.region_width, cfg.region_height, cfg.region_x,
                          cfg.region_y, capture_.width(), capture_.height());
            error = message;
            return false;
        }
        width_ = cfg.region_width;
        height_ = cfg.region_height;
        source_rect_ = {cfg.region_x, cfg.region_y,
                        cfg.region_x + cfg.region_width,
                        cfg.region_y + cfg.region_height};
        return true;
    }

    width_ = capture_.width();
    height_ = capture_.height();
    source_rect_ = {0, 0, width_, height_};
    return true;
}

bool DesktopSource::Acquire(SourceFrame& frame, int timeout_ms) {
    CaptureFrame captured;
    if (!capture_.Acquire(captured, timeout_ms)) {
        return false;
    }
    frame.texture = captured.texture.Get();
    frame.width = width_;
    frame.height = height_;
    frame.source_rect = source_rect_;
    frame.new_frame = captured.new_desktop;
    frame.hdr = captured.hdr;
    frame.format = captured.format;
    return true;
}

}  // namespace hmmob
