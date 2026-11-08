#pragma once
#include <d3d11.h>
#include <windows.h>

#include <string>

#include "core/gfx/D3D11Device.h"
#include "core/recorder/RecorderConfig.h"

namespace hmmob {

// One acquired frame. `texture` is owned by the source and is only valid until
// the next Acquire(); consumers must convert or copy it before then.
struct SourceFrame {
    ID3D11Texture2D* texture = nullptr;
    // Encoded size: the size of the recording, not necessarily the texture
    // size. A region source delivers a full display texture whose `source_rect`
    // is the area to encode.
    int width = 0;
    int height = 0;
    // Valid area inside `texture` that maps to width x height.
    RECT source_rect{};
    bool new_frame = false;
    bool hdr = false;
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
};

// Capture backend consumed by Recorder. Implementations own their frames and
// are used from a single recording thread.
class VideoSource {
public:
    virtual ~VideoSource() = default;

    virtual bool Initialize(D3D11Device& d3d, const RecorderConfig& cfg,
                            std::string& error) = 0;
    // Returns false on timeout or failure; check failed()/access_lost() to
    // distinguish the two.
    virtual bool Acquire(SourceFrame& frame, int timeout_ms) = 0;

    // Encoded size of the recording.
    virtual int width() const = 0;
    virtual int height() const = 0;
    // Size of the texture delivered by Acquire(). Equal to width()/height()
    // unless the source crops.
    virtual int texture_width() const { return width(); }
    virtual int texture_height() const { return height(); }
    virtual DXGI_FORMAT format() const = 0;
    virtual DXGI_COLOR_SPACE_TYPE color_space() const = 0;
    // True when delivered frames carry an HDR format.
    virtual bool hdr_capable() const = 0;
    virtual bool hdr_downgraded() const { return false; }
    virtual bool failed() const { return false; }
    virtual bool access_lost() const { return false; }
    virtual HRESULT last_error() const { return S_OK; }
    // Human readable description of the last failure. Empty when there is
    // none or the source does not provide one.
    virtual std::string last_message() const { return std::string(); }
};

}  // namespace hmmob
