#include "core/gfx/VideoProcessor.h"
#include "core/common/Check.h"

namespace hmmob {

bool VideoProcessor::Initialize(D3D11Device& d3d, int width, int height) {
    if (width <= 0 || height <= 0) {
        return false;
    }
    device_ = d3d.device();
    context_ = d3d.context();
    width_ = width;
    height_ = height;

    HMMOB_HR_CHECK(device_->QueryInterface(IID_PPV_ARGS(&video_device_)));
    HMMOB_HR_CHECK(context_->QueryInterface(IID_PPV_ARGS(&video_context_)));

    D3D11_VIDEO_PROCESSOR_CONTENT_DESC content{};
    content.InputFrameFormat = D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;
    content.InputFrameRate = {60, 1};
    content.InputWidth = static_cast<UINT>(width);
    content.InputHeight = static_cast<UINT>(height);
    content.OutputFrameRate = {60, 1};
    content.OutputWidth = static_cast<UINT>(width);
    content.OutputHeight = static_cast<UINT>(height);
    content.Usage = D3D11_VIDEO_USAGE_PLAYBACK_NORMAL;

    HMMOB_HR_CHECK(video_device_->CreateVideoProcessorEnumerator(&content, &enumerator_));
    HMMOB_HR_CHECK(video_device_->CreateVideoProcessor(enumerator_.Get(), 0, &processor_));

    D3D11_TEXTURE2D_DESC out_desc{};
    out_desc.Width = static_cast<UINT>(width);
    out_desc.Height = static_cast<UINT>(height);
    out_desc.MipLevels = 1;
    out_desc.ArraySize = 1;
    out_desc.Format = DXGI_FORMAT_NV12;
    out_desc.SampleDesc.Count = 1;
    out_desc.Usage = D3D11_USAGE_DEFAULT;
    out_desc.BindFlags = D3D11_BIND_RENDER_TARGET;
    HMMOB_HR_CHECK(device_->CreateTexture2D(&out_desc, nullptr, &output_));

    D3D11_VIDEO_PROCESSOR_COLOR_SPACE input_cs{};
    input_cs.Usage = 0;  // playback
    input_cs.RGB_Range = 0;    // full range (0-255)
    input_cs.YCbCr_Matrix = 1; // BT.709
    input_cs.Nominal_Range = D3D11_VIDEO_PROCESSOR_NOMINAL_RANGE_0_255;
    video_context_->VideoProcessorSetStreamColorSpace(processor_.Get(), 0, &input_cs);

    D3D11_VIDEO_PROCESSOR_COLOR_SPACE output_cs{};
    output_cs.Usage = 0;  // playback
    output_cs.RGB_Range = 0;
    output_cs.YCbCr_Matrix = 1; // BT.709
    output_cs.Nominal_Range = D3D11_VIDEO_PROCESSOR_NOMINAL_RANGE_16_235;
    video_context_->VideoProcessorSetOutputColorSpace(processor_.Get(), &output_cs);
    return true;
}

bool VideoProcessor::Convert(ID3D11Texture2D* bgra, ID3D11Texture2D* nv12) {
    if (!processor_ || !bgra || !nv12) {
        return false;
    }

    D3D11_TEXTURE2D_DESC src_desc{};
    bgra->GetDesc(&src_desc);
    if (src_desc.Format != DXGI_FORMAT_B8G8R8A8_UNORM ||
        src_desc.Width != static_cast<UINT>(width_) ||
        src_desc.Height != static_cast<UINT>(height_)) {
        return false;
    }

    D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC input_desc{};
    input_desc.FourCC = 0;
    input_desc.ViewDimension = D3D11_VPIV_DIMENSION_TEXTURE2D;
    input_desc.Texture2D.MipSlice = 0;
    input_desc.Texture2D.ArraySlice = 0;

    Microsoft::WRL::ComPtr<ID3D11VideoProcessorInputView> input_view;
    HMMOB_HR_CHECK(video_device_->CreateVideoProcessorInputView(
        bgra, enumerator_.Get(), &input_desc, &input_view));

    D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC out_desc{};
    out_desc.ViewDimension = D3D11_VPOV_DIMENSION_TEXTURE2D;
    out_desc.Texture2D.MipSlice = 0;
    Microsoft::WRL::ComPtr<ID3D11VideoProcessorOutputView> out_view;
    HMMOB_HR_CHECK(video_device_->CreateVideoProcessorOutputView(
        nv12, enumerator_.Get(), &out_desc, &out_view));

    D3D11_VIDEO_PROCESSOR_STREAM stream{};
    stream.Enable = TRUE;
    stream.pInputSurface = input_view.Get();

    HMMOB_HR_CHECK(video_context_->VideoProcessorBlt(
        processor_.Get(), out_view.Get(), 0, 1, &stream));
    return true;
}

}  // namespace hmmob
