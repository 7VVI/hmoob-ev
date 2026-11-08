#pragma once
#include <windows.h>

#include <d3d11.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wrl/client.h>

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "core/capture/VideoSource.h"
#include "core/gfx/D3D11Device.h"

namespace hmmob {

// Media Foundation backed source for one video capture device. A reader thread
// pulls the newest RGB32 sample and copies it into a packed BGRA buffer, which
// Acquire() uploads into a texture owned by this source. Capture is SDR BGRA8
// at the negotiated size rounded down to even dimensions.
//
// The encoded resolution is fixed for the lifetime of the recording: a camera
// that disconnects or stops delivering samples ends the recording with an
// error instead of producing a broken file.
class CameraSource final : public VideoSource {
public:
    struct CameraDeviceInfo {
        std::string name;
        std::string id;
    };

    // Captures devices may be absent on a machine with no camera; the result
    // is then empty rather than an error. Starts and stops Media Foundation for
    // the duration of the enumeration.
    static std::vector<CameraDeviceInfo> EnumerateDevices();

    CameraSource() = default;
    ~CameraSource() override;

    CameraSource(const CameraSource&) = delete;
    CameraSource& operator=(const CameraSource&) = delete;

    bool Initialize(D3D11Device& d3d, const RecorderConfig& cfg,
                    std::string& error) override;
    // Never waits for the device: returns the cached texture when no new
    // sample has arrived since the previous call.
    bool Acquire(SourceFrame& frame, int timeout_ms) override;

    // Encoded size: the negotiated size rounded down to even dimensions
    // because the NV12 encoder path cannot handle odd sizes.
    int width() const override { return width_; }
    int height() const override { return height_; }
    int texture_width() const override { return width_; }
    int texture_height() const override { return height_; }
    DXGI_FORMAT format() const override { return DXGI_FORMAT_B8G8R8A8_UNORM; }
    DXGI_COLOR_SPACE_TYPE color_space() const override {
        return DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
    }
    bool hdr_capable() const override { return false; }
    bool failed() const override { return failed_.load(); }
    HRESULT last_error() const override;
    std::string last_message() const override;

private:
    // Stops the reader thread, releases everything and stops Media Foundation
    // for the pair this instance started. Safe to call more than once.
    void Close();
    // Reads samples until stopped or the reader reports a failure.
    void ReaderLoop();
    // Copies one RGB32 sample into the packed frame buffer, honoring the
    // source stride.
    bool CopySample(IMFSample* sample);
    void RecordFailure(std::string message, HRESULT hr);
    [[nodiscard]] bool CreateOwnedTexture();

    Microsoft::WRL::ComPtr<ID3D11Device> device_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture_;
    Microsoft::WRL::ComPtr<IMFActivate> activate_;
    Microsoft::WRL::ComPtr<IMFMediaSource> media_source_;
    Microsoft::WRL::ComPtr<IMFSourceReader> reader_;

    std::thread reader_thread_;
    std::atomic<bool> stop_reader_{false};
    std::atomic<bool> failed_{false};

    // Guards the published frame buffer and sequence number, plus the failure
    // description written by the reader thread.
    mutable std::mutex mutex_;
    std::vector<uint8_t> frame_buffer_;
    uint64_t sequence_ = 0;
    uint64_t uploaded_sequence_ = 0;
    std::string last_message_;
    HRESULT last_error_ = S_OK;

    int width_ = 0;
    int height_ = 0;
    int source_width_ = 0;
    int source_height_ = 0;
    LONG source_stride_ = 0;
    bool mf_started_ = false;
};

}  // namespace hmmob
