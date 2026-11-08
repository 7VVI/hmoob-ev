#include "core/gfx/VideoProcessor.h"

#include <cstdio>

namespace hmmob {
namespace {

void LogHresult(const char* file, int line, HRESULT hr) {
    std::fprintf(stderr, "%s:%d HRESULT=0x%08lx\n", file, line,
                 static_cast<unsigned long>(hr));
}

}  // namespace

void VideoProcessor::Reset() {
    input_view_.Reset();
    output_view_.Reset();
    processor_.Reset();
    enumerator_.Reset();
    video_context_.Reset();
    video_device_.Reset();
    output_.Reset();
    cached_input_ = nullptr;
    device_ = nullptr;
    context_ = nullptr;
    input_width_ = 0;
    input_height_ = 0;
    output_width_ = 0;
    output_height_ = 0;
    output_format_ = DXGI_FORMAT_NV12;
    ready_ = false;
}

bool VideoProcessor::Initialize(D3D11Device& d3d, int width, int height) {
    return Initialize(d3d, width, height, width, height, DXGI_FORMAT_NV12);
}

bool VideoProcessor::Initialize(D3D11Device& d3d, int input_width,
                                int input_height, int output_width,
                                int output_height, DXGI_FORMAT output_format) {
    Reset();
    last_error_ = S_OK;

    const bool format_supported =
        output_format == DXGI_FORMAT_NV12 ||
        output_format == DXGI_FORMAT_B8G8R8A8_UNORM;
    if (!d3d.device() || !d3d.context() || input_width <= 0 || input_height <= 0 ||
        output_width <= 0 || output_height <= 0 || !format_supported) {
        last_error_ = E_INVALIDARG;
        return false;
    }
    device_ = d3d.device();
    context_ = d3d.context();
    input_width_ = input_width;
    input_height_ = input_height;
    output_width_ = output_width;
    output_height_ = output_height;
    output_format_ = output_format;

    HRESULT hr = device_->QueryInterface(IID_PPV_ARGS(&video_device_));
    if (FAILED(hr)) {
        LogHresult(__FILE__, __LINE__, hr);
        last_error_ = hr;
        Reset();
        return false;
    }
    hr = context_->QueryInterface(IID_PPV_ARGS(&video_context_));
    if (FAILED(hr)) {
        LogHresult(__FILE__, __LINE__, hr);
        last_error_ = hr;
        Reset();
        return false;
    }

    D3D11_VIDEO_PROCESSOR_CONTENT_DESC content{};
    content.InputFrameFormat = D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;
    // Phase 0 placeholder; Phase 1 threads the real capture cadence.
    content.InputFrameRate = {60, 1};
    content.InputWidth = static_cast<UINT>(input_width);
    content.InputHeight = static_cast<UINT>(input_height);
    // Phase 0 placeholder; Phase 1 threads the real capture cadence.
    content.OutputFrameRate = {60, 1};
    content.OutputWidth = static_cast<UINT>(output_width);
    content.OutputHeight = static_cast<UINT>(output_height);
    content.Usage = D3D11_VIDEO_USAGE_PLAYBACK_NORMAL;

    hr = video_device_->CreateVideoProcessorEnumerator(&content, &enumerator_);
    if (FAILED(hr)) {
        LogHresult(__FILE__, __LINE__, hr);
        last_error_ = hr;
        Reset();
        return false;
    }
    hr = video_device_->CreateVideoProcessor(enumerator_.Get(), 0, &processor_);
    if (FAILED(hr)) {
        LogHresult(__FILE__, __LINE__, hr);
        last_error_ = hr;
        Reset();
        return false;
    }

    D3D11_TEXTURE2D_DESC out_desc{};
    out_desc.Width = static_cast<UINT>(output_width);
    out_desc.Height = static_cast<UINT>(output_height);
    out_desc.MipLevels = 1;
    out_desc.ArraySize = 1;
    out_desc.Format = output_format_;
    out_desc.SampleDesc.Count = 1;
    out_desc.Usage = D3D11_USAGE_DEFAULT;
    out_desc.BindFlags = D3D11_BIND_RENDER_TARGET;
    hr = device_->CreateTexture2D(&out_desc, nullptr, &output_);
    if (FAILED(hr)) {
        LogHresult(__FILE__, __LINE__, hr);
        last_error_ = hr;
        Reset();
        return false;
    }

    D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC out_view_desc{};
    out_view_desc.ViewDimension = D3D11_VPOV_DIMENSION_TEXTURE2D;
    out_view_desc.Texture2D.MipSlice = 0;
    hr = video_device_->CreateVideoProcessorOutputView(
        output_.Get(), enumerator_.Get(), &out_view_desc, &output_view_);
    if (FAILED(hr)) {
        LogHresult(__FILE__, __LINE__, hr);
        last_error_ = hr;
        Reset();
        return false;
    }

    D3D11_VIDEO_PROCESSOR_COLOR_SPACE input_cs{};
    input_cs.Usage = 0;         // playback
    input_cs.RGB_Range = 0;     // full range (0-255)
    input_cs.YCbCr_Matrix = 1;  // BT.709
    input_cs.Nominal_Range = D3D11_VIDEO_PROCESSOR_NOMINAL_RANGE_0_255;
    video_context_->VideoProcessorSetStreamColorSpace(processor_.Get(), 0, &input_cs);

    D3D11_VIDEO_PROCESSOR_COLOR_SPACE output_cs{};
    output_cs.Usage = 0;        // playback
    output_cs.RGB_Range = 0;
    output_cs.YCbCr_Matrix = 1; // BT.709
    output_cs.Nominal_Range =
        output_format_ == DXGI_FORMAT_B8G8R8A8_UNORM
            ? D3D11_VIDEO_PROCESSOR_NOMINAL_RANGE_0_255
            : D3D11_VIDEO_PROCESSOR_NOMINAL_RANGE_16_235;
    video_context_->VideoProcessorSetOutputColorSpace(processor_.Get(), &output_cs);

    ready_ = true;
    return true;
}

bool VideoProcessor::Convert(ID3D11Texture2D* input, ID3D11Texture2D* output) {
    if (!input) {
        return false;
    }
    D3D11_TEXTURE2D_DESC desc{};
    input->GetDesc(&desc);
    const RECT full{0, 0, static_cast<LONG>(desc.Width),
                    static_cast<LONG>(desc.Height)};
    return Convert(input, output, full);
}

bool VideoProcessor::Convert(ID3D11Texture2D* input, ID3D11Texture2D* output,
                             const RECT& source_rect) {
    if (!ready_ || !processor_ || !video_device_ || !video_context_ || !enumerator_ ||
        !output_ || !output_view_ || !input || !output) {
        return false;
    }
    if (output != output_.Get()) {
        return false;
    }

    D3D11_TEXTURE2D_DESC dst_desc{};
    output->GetDesc(&dst_desc);
    if (dst_desc.Format != output_format_ ||
        dst_desc.Width != static_cast<UINT>(output_width_) ||
        dst_desc.Height != static_cast<UINT>(output_height_) ||
        dst_desc.ArraySize != 1) {
        return false;
    }

    D3D11_TEXTURE2D_DESC src_desc{};
    input->GetDesc(&src_desc);
    if (src_desc.Format != DXGI_FORMAT_B8G8R8A8_UNORM ||
        src_desc.Width != static_cast<UINT>(input_width_) ||
        src_desc.Height != static_cast<UINT>(input_height_)) {
        return false;
    }

    if (source_rect.left < 0 || source_rect.top < 0 ||
        source_rect.right <= source_rect.left ||
        source_rect.bottom <= source_rect.top ||
        source_rect.right > static_cast<LONG>(src_desc.Width) ||
        source_rect.bottom > static_cast<LONG>(src_desc.Height)) {
        return false;
    }

    // The cached input view holds a COM reference to its texture, so a released
    // texture's address cannot be reused while cached (ABA-safe). The last
    // input texture and its view are retained until Reset/re-initialization.
    if (cached_input_ != input) {
        input_view_.Reset();
        cached_input_ = nullptr;

        D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC input_desc{};
        input_desc.FourCC = 0;
        input_desc.ViewDimension = D3D11_VPIV_DIMENSION_TEXTURE2D;
        input_desc.Texture2D.MipSlice = 0;
        input_desc.Texture2D.ArraySlice = 0;

        const HRESULT hr = video_device_->CreateVideoProcessorInputView(
            input, enumerator_.Get(), &input_desc, &input_view_);
        if (FAILED(hr)) {
            LogHresult(__FILE__, __LINE__, hr);
            return false;
        }
        cached_input_ = input;
    }

    if (!input_view_) {
        return false;
    }

    // Crop the requested area of the input into the whole output texture.
    const RECT dest_rect{0, 0, static_cast<LONG>(output_width_),
                         static_cast<LONG>(output_height_)};
    video_context_->VideoProcessorSetStreamSourceRect(processor_.Get(), 0, TRUE,
                                                      &source_rect);
    video_context_->VideoProcessorSetStreamDestRect(processor_.Get(), 0, TRUE,
                                                    &dest_rect);

    D3D11_VIDEO_PROCESSOR_STREAM stream{};
    stream.Enable = TRUE;
    stream.pInputSurface = input_view_.Get();

    const HRESULT hr = video_context_->VideoProcessorBlt(
        processor_.Get(), output_view_.Get(), 0, 1, &stream);
    if (FAILED(hr)) {
        LogHresult(__FILE__, __LINE__, hr);
        return false;
    }
    return true;
}

}  // namespace hmmob
