#include "core/capture/CameraSource.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <utility>

namespace hmmob {
namespace {

using Microsoft::WRL::ComPtr;

constexpr char kCameraDisconnected[] = "camera disconnected";
constexpr char kCameraReadFailed[] = "camera read failed";

std::string HresultMessage(const char* what, HRESULT hr) {
    char buffer[192];
    std::snprintf(buffer, sizeof(buffer), "%s (0x%08lx)", what,
                  static_cast<unsigned long>(hr));
    return buffer;
}

std::string WideToUtf8(const std::wstring& text) {
    if (text.empty()) {
        return {};
    }
    const int length =
        WideCharToMultiByte(CP_UTF8, 0, text.data(),
                            static_cast<int>(text.size()), nullptr, 0, nullptr,
                            nullptr);
    if (length <= 0) {
        return {};
    }
    std::string result(static_cast<size_t>(length), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(),
                        static_cast<int>(text.size()), result.data(), length,
                        nullptr, nullptr);
    return result;
}

// Builds a request for RGB32 output. Zero width/height leaves the frame size
// unspecified so the source reader can pick a native mode.
ComPtr<IMFMediaType> MakeRgb32Type(UINT32 width, UINT32 height,
                                   UINT32 fps_numerator) {
    ComPtr<IMFMediaType> type;
    if (FAILED(MFCreateMediaType(&type))) {
        return nullptr;
    }
    type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    type->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
    type->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    type->SetUINT32(MF_MT_ALL_SAMPLES_INDEPENDENT, TRUE);
    if (width > 0 && height > 0) {
        MFSetAttributeSize(type.Get(), MF_MT_FRAME_SIZE, width, height);
    }
    if (fps_numerator > 0) {
        MFSetAttributeRatio(type.Get(), MF_MT_FRAME_RATE, fps_numerator, 1);
    }
    return type;
}

}  // namespace

CameraSource::~CameraSource() { Close(); }

std::vector<CameraSource::CameraDeviceInfo> CameraSource::EnumerateDevices() {
    std::vector<CameraDeviceInfo> devices;
    if (FAILED(MFStartup(MF_VERSION))) {
        return devices;
    }

    ComPtr<IMFAttributes> attributes;
    IMFActivate** activations = nullptr;
    UINT32 count = 0;
    if (SUCCEEDED(MFCreateAttributes(&attributes, 1)) &&
        SUCCEEDED(attributes->SetGUID(
            MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
            MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID)) &&
        SUCCEEDED(MFEnumDeviceSources(attributes.Get(), &activations,
                                      &count))) {
        devices.reserve(count);
        for (UINT32 i = 0; i < count; ++i) {
            CameraDeviceInfo info;
            WCHAR* value = nullptr;
            UINT32 length = 0;
            if (SUCCEEDED(activations[i]->GetAllocatedString(
                    MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, &value, &length))) {
                info.name = WideToUtf8(std::wstring(value, length));
                CoTaskMemFree(value);
            }
            value = nullptr;
            length = 0;
            if (SUCCEEDED(activations[i]->GetAllocatedString(
                    MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK,
                    &value, &length))) {
                info.id = WideToUtf8(std::wstring(value, length));
                CoTaskMemFree(value);
            }
            if (info.name.empty()) {
                char fallback[32];
                std::snprintf(fallback, sizeof(fallback), "Camera %u", i + 1);
                info.name = fallback;
            }
            devices.push_back(std::move(info));
            activations[i]->Release();
        }
        CoTaskMemFree(activations);
    }

    MFShutdown();
    return devices;
}

void CameraSource::RecordFailure(std::string message, HRESULT hr) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (last_message_.empty()) {
            last_message_ = std::move(message);
            last_error_ = hr;
        }
    }
    failed_ = true;
}

HRESULT CameraSource::last_error() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return last_error_;
}

std::string CameraSource::last_message() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return last_message_;
}

void CameraSource::Close() {
    stop_reader_ = true;
    if (reader_thread_.joinable()) {
        reader_thread_.join();
    }
    reader_.Reset();
    if (media_source_) {
        media_source_->Shutdown();
        media_source_.Reset();
    }
    activate_.Reset();
    texture_.Reset();
    context_.Reset();
    device_.Reset();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        frame_buffer_.clear();
        sequence_ = 0;
        uploaded_sequence_ = 0;
    }
    width_ = 0;
    height_ = 0;
    source_width_ = 0;
    source_height_ = 0;
    source_stride_ = 0;
    if (mf_started_) {
        MFShutdown();
        mf_started_ = false;
    }
}

bool CameraSource::CreateOwnedTexture() {
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = static_cast<UINT>(width_);
    desc.Height = static_cast<UINT>(height_);
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    // CreateVideoProcessorInputView requires a render-target texture; the
    // recorder feeds this texture straight into its BGRA8 -> NV12 converter.
    desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    const HRESULT hr = device_->CreateTexture2D(
        &desc, nullptr, texture_.ReleaseAndGetAddressOf());
    if (FAILED(hr)) {
        std::lock_guard<std::mutex> lock(mutex_);
        last_error_ = hr;
        return false;
    }
    return true;
}

bool CameraSource::Initialize(D3D11Device& d3d, const RecorderConfig& cfg,
                              std::string& error) {
    Close();
    failed_ = false;
    stop_reader_ = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        last_error_ = S_OK;
        last_message_.clear();
        sequence_ = 0;
        uploaded_sequence_ = 0;
    }

    if (!d3d.device() || !d3d.context()) {
        error = "d3d11 device is unavailable";
        return false;
    }
    device_ = d3d.device();
    context_ = d3d.context();

    const auto fail = [&](std::string message) {
        error = std::move(message);
        Close();
        return false;
    };

    const HRESULT mf_hr = MFStartup(MF_VERSION);
    if (FAILED(mf_hr)) {
        return fail(HresultMessage("failed to initialize media foundation",
                                   mf_hr));
    }
    mf_started_ = true;

    ComPtr<IMFAttributes> attributes;
    HRESULT hr = MFCreateAttributes(&attributes, 1);
    if (FAILED(hr)) {
        return fail(HresultMessage("failed to initialize media foundation", hr));
    }
    hr = attributes->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
                             MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
    if (FAILED(hr)) {
        return fail(HresultMessage("failed to initialize media foundation", hr));
    }

    IMFActivate** activations = nullptr;
    UINT32 count = 0;
    hr = MFEnumDeviceSources(attributes.Get(), &activations, &count);
    if (FAILED(hr)) {
        return fail(HresultMessage("failed to enumerate cameras", hr));
    }
    if (count == 0) {
        CoTaskMemFree(activations);
        return fail("no camera found");
    }
    if (cfg.camera_index < 0 ||
        cfg.camera_index >= static_cast<int>(count)) {
        for (UINT32 i = 0; i < count; ++i) {
            activations[i]->Release();
        }
        CoTaskMemFree(activations);
        char message[96];
        std::snprintf(message, sizeof(message),
                      "camera index out of range (0..%u)", count - 1);
        return fail(message);
    }
    activate_.Attach(activations[cfg.camera_index]);
    for (UINT32 i = 0; i < count; ++i) {
        if (static_cast<int>(i) != cfg.camera_index) {
            activations[i]->Release();
        }
    }
    CoTaskMemFree(activations);

    hr = activate_->ActivateObject(IID_PPV_ARGS(
        media_source_.ReleaseAndGetAddressOf()));
    if (FAILED(hr)) {
        return fail(HresultMessage("failed to open the camera", hr));
    }

    // Advanced video processing lets the source reader insert the video
    // processor MFT, which converts the native format to RGB32 and scales the
    // picture to the requested size.
    ComPtr<IMFAttributes> reader_attributes;
    hr = MFCreateAttributes(&reader_attributes, 1);
    if (FAILED(hr)) {
        return fail(HresultMessage("failed to initialize the camera", hr));
    }
    reader_attributes->SetUINT32(
        MF_SOURCE_READER_ENABLE_ADVANCED_VIDEO_PROCESSING, TRUE);
    hr = MFCreateSourceReaderFromMediaSource(
        media_source_.Get(), reader_attributes.Get(),
        reader_.ReleaseAndGetAddressOf());
    if (FAILED(hr)) {
        return fail(HresultMessage("failed to open the camera reader", hr));
    }

    // Only the video stream is wanted; leaving audio selected would make the
    // reader deliver samples this source never drains.
    reader_->SetStreamSelection(
        static_cast<DWORD>(MF_SOURCE_READER_ALL_STREAMS), FALSE);
    hr = reader_->SetStreamSelection(
        static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), TRUE);
    if (FAILED(hr)) {
        return fail(HresultMessage("the camera has no video stream", hr));
    }

    // Prefer 1080p30. Devices that reject it fall back to their native size at
    // 30 fps, then to whatever the reader can negotiate.
    HRESULT type_hr = E_FAIL;
    ComPtr<IMFMediaType> output_type = MakeRgb32Type(1920, 1080, 30);
    if (output_type) {
        type_hr = reader_->SetCurrentMediaType(
            static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), nullptr,
            output_type.Get());
    }
    if (FAILED(type_hr)) {
        ComPtr<IMFMediaType> native;
        if (SUCCEEDED(reader_->GetNativeMediaType(
                static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), 0,
                native.ReleaseAndGetAddressOf()))) {
            UINT32 native_width = 0;
            UINT32 native_height = 0;
            if (SUCCEEDED(MFGetAttributeSize(native.Get(), MF_MT_FRAME_SIZE,
                                             &native_width, &native_height)) &&
                native_width > 0 && native_height > 0) {
                output_type =
                    MakeRgb32Type(native_width, native_height, 30);
                if (output_type) {
                    type_hr = reader_->SetCurrentMediaType(
                        static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM),
                        nullptr, output_type.Get());
                }
                if (FAILED(type_hr)) {
                    output_type =
                        MakeRgb32Type(native_width, native_height, 0);
                    if (output_type) {
                        type_hr = reader_->SetCurrentMediaType(
                            static_cast<DWORD>(
                                MF_SOURCE_READER_FIRST_VIDEO_STREAM),
                            nullptr, output_type.Get());
                    }
                }
            }
        }
    }
    if (FAILED(type_hr)) {
        output_type = MakeRgb32Type(0, 0, 0);
        if (output_type) {
            type_hr = reader_->SetCurrentMediaType(
                static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM),
                nullptr, output_type.Get());
        }
    }
    if (FAILED(type_hr)) {
        return fail(
            HresultMessage("the camera does not support rgb32 output", type_hr));
    }

    ComPtr<IMFMediaType> current;
    hr = reader_->GetCurrentMediaType(
        static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM),
        current.ReleaseAndGetAddressOf());
    if (FAILED(hr)) {
        return fail(HresultMessage("failed to read the camera format", hr));
    }
    GUID subtype{};
    if (FAILED(current->GetGUID(MF_MT_SUBTYPE, &subtype)) ||
        subtype != MFVideoFormat_RGB32) {
        return fail("the camera reader did not produce rgb32 frames");
    }

    UINT32 frame_width = 0;
    UINT32 frame_height = 0;
    hr = MFGetAttributeSize(current.Get(), MF_MT_FRAME_SIZE, &frame_width,
                            &frame_height);
    if (FAILED(hr) || frame_width < 2 || frame_height < 2) {
        return fail("failed to read the camera frame size");
    }
    source_width_ = static_cast<int>(frame_width);
    source_height_ = static_cast<int>(frame_height);
    // The NV12 encoder path needs even dimensions, so the encoded size drops
    // the last row and/or column (at most one pixel). The texture is created
    // at the even size and the sample copy crops that same area, so no
    // source_rect crop is needed.
    width_ = source_width_ & ~1;
    height_ = source_height_ & ~1;

    UINT32 stride = 0;
    if (SUCCEEDED(current->GetUINT32(MF_MT_DEFAULT_STRIDE, &stride))) {
        source_stride_ = static_cast<LONG>(static_cast<INT32>(stride));
    }
    const LONG minimum_stride = static_cast<LONG>(source_width_) * 4;
    const LONG absolute_stride =
        source_stride_ < 0 ? -source_stride_ : source_stride_;
    if (source_stride_ == 0 || absolute_stride < minimum_stride) {
        source_stride_ = minimum_stride;
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        frame_buffer_.assign(
            static_cast<size_t>(width_) * static_cast<size_t>(height_) * 4, 0);
    }
    if (!CreateOwnedTexture()) {
        return fail(HresultMessage("failed to create the capture texture",
                                   last_error()));
    }

    if (!reader_thread_.joinable()) {
        try {
            reader_thread_ = std::thread(&CameraSource::ReaderLoop, this);
        } catch (...) {
            return fail("failed to start the camera reader thread");
        }
    }
    return true;
}

void CameraSource::ReaderLoop() {
    // The recording thread initialized COM as MTA, but COM is per thread.
    const HRESULT com_hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool uninit_com = SUCCEEDED(com_hr);
    if (FAILED(com_hr) && com_hr != RPC_E_CHANGED_MODE) {
        RecordFailure(HresultMessage("failed to initialize com", com_hr),
                      com_hr);
        return;
    }

    while (!stop_reader_.load(std::memory_order_relaxed)) {
        DWORD flags = 0;
        LONGLONG timestamp = 0;
        ComPtr<IMFSample> sample;
        const HRESULT hr = reader_->ReadSample(
            static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), 0,
            nullptr, &flags, &timestamp,
            sample.ReleaseAndGetAddressOf());
        (void)timestamp;
        if (FAILED(hr)) {
            RecordFailure(HresultMessage(kCameraReadFailed, hr), hr);
            break;
        }
        if ((flags & MF_SOURCE_READERF_ERROR) != 0) {
            RecordFailure(kCameraReadFailed, E_FAIL);
            break;
        }
        if ((flags & MF_SOURCE_READERF_ENDOFSTREAM) != 0) {
            RecordFailure(
                kCameraDisconnected,
                HRESULT_FROM_WIN32(ERROR_DEVICE_NOT_CONNECTED));
            break;
        }
        if (stop_reader_.load(std::memory_order_relaxed)) {
            break;
        }
        if ((flags & MF_SOURCE_READERF_STREAMTICK) != 0) {
            // A gap in the stream, not a frame.
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            continue;
        }
        if (!sample) {
            // No sample ready yet (for example a stream tick): back off a
            // little instead of spinning.
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            continue;
        }
        if (!CopySample(sample.Get())) {
            break;
        }
    }

    if (uninit_com) {
        CoUninitialize();
    }
}

bool CameraSource::CopySample(IMFSample* sample) {
    ComPtr<IMFMediaBuffer> buffer;
    HRESULT hr = sample->ConvertToContiguousBuffer(
        buffer.ReleaseAndGetAddressOf());
    if (FAILED(hr)) {
        RecordFailure(HresultMessage(kCameraReadFailed, hr), hr);
        return false;
    }

    BYTE* data = nullptr;
    DWORD max_length = 0;
    DWORD length = 0;
    hr = buffer->Lock(&data, &max_length, &length);
    if (FAILED(hr)) {
        RecordFailure(HresultMessage(kCameraReadFailed, hr), hr);
        return false;
    }
    if (length == 0) {
        length = max_length;
    }

    const DWORD row_bytes = static_cast<DWORD>(width_) * 4;
    const DWORD absolute_stride = static_cast<DWORD>(
        source_stride_ < 0 ? -source_stride_ : source_stride_);
    DWORD lines = 0;
    if (length >= row_bytes && absolute_stride > 0) {
        lines = (length - row_bytes) / absolute_stride + 1;
    }
    if (lines > static_cast<DWORD>(height_)) {
        lines = static_cast<DWORD>(height_);
    }
    if (lines < static_cast<DWORD>(height_)) {
        buffer->Unlock();
        RecordFailure("camera delivered a truncated frame", E_FAIL);
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        // MFCopyImage honors a negative source stride (bottom-up images).
        MFCopyImage(frame_buffer_.data(), static_cast<LONG>(row_bytes), data,
                    source_stride_, row_bytes, lines);
        ++sequence_;
    }
    buffer->Unlock();
    return true;
}

bool CameraSource::Acquire(SourceFrame& frame, int timeout_ms) {
    // The reader thread publishes frames asynchronously; the recording thread
    // must never wait for the device.
    (void)timeout_ms;
    if (!texture_ || !reader_ || failed_.load()) {
        return false;
    }

    bool updated = false;
    bool have_frame = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (sequence_ > uploaded_sequence_ && !frame_buffer_.empty()) {
            context_->UpdateSubresource(
                texture_.Get(), 0, nullptr, frame_buffer_.data(),
                static_cast<UINT>(width_) * 4, 0);
            uploaded_sequence_ = sequence_;
            updated = true;
        }
        have_frame = uploaded_sequence_ > 0;
    }
    if (!have_frame) {
        // Nothing captured yet; the reader thread may still be starting.
        return false;
    }

    frame.texture = texture_.Get();
    frame.width = width_;
    frame.height = height_;
    frame.source_rect = RECT{0, 0, width_, height_};
    frame.new_frame = updated;
    frame.hdr = false;
    frame.format = DXGI_FORMAT_B8G8R8A8_UNORM;
    return true;
}

}  // namespace hmmob
