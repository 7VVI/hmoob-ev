#include "core/encoder/AmfEncoder.h"

#include <windows.h>
#include <core/Context.h>
#include <core/Factory.h>
#include <core/Surface.h>
#include <core/Buffer.h>
#include <components/VideoEncoderHEVC.h>
#include <components/VideoEncoderVCE.h>
#include <cstdio>

namespace hmmob {

namespace {
// AMF PTS is expressed in 100-nanosecond units.
constexpr int64_t kAmfPtsPerUs = 10;

void LogAmfError(const char* what, AMF_RESULT result) {
    std::fprintf(stderr, "AMF %s failed: %d\n", what, static_cast<int>(result));
}
}  // namespace

struct AmfEncoder::Impl {
    HMODULE module = nullptr;
    amf::AMFFactory* factory = nullptr;
    amf::AMFContextPtr context;
    amf::AMFComponentPtr encoder;
    int width = 0;
    int height = 0;
    int fps = 0;
    std::string codec;

    bool Drain(const PacketCallback& callback) {
        for (;;) {
            amf::AMFDataPtr data;
            AMF_RESULT result = encoder->QueryOutput(&data);
            if (result == AMF_EOF) {
                return true;
            }
            // No output ready is not an encoding failure; the encoder may buffer.
            if (result != AMF_OK || !data) {
                return true;
            }

            amf::AMFBufferPtr buffer(data);
            if (!buffer) {
                continue;
            }

            AmfPacket packet;
            packet.pts_us = data->GetPts() / kAmfPtsPerUs;
            const auto* bytes = static_cast<const uint8_t*>(buffer->GetNative());
            packet.data.assign(bytes, bytes + buffer->GetSize());

            amf_int64 type = 0;
            if (codec == "hevc") {
                if (data->GetProperty(AMF_VIDEO_ENCODER_HEVC_OUTPUT_DATA_TYPE, &type) == AMF_OK) {
                    packet.keyframe =
                        type == AMF_VIDEO_ENCODER_HEVC_OUTPUT_DATA_TYPE_IDR ||
                        type == AMF_VIDEO_ENCODER_HEVC_OUTPUT_DATA_TYPE_I;
                }
            } else if (data->GetProperty(AMF_VIDEO_ENCODER_OUTPUT_DATA_TYPE, &type) == AMF_OK) {
                packet.keyframe = type == AMF_VIDEO_ENCODER_OUTPUT_DATA_TYPE_IDR ||
                                  type == AMF_VIDEO_ENCODER_OUTPUT_DATA_TYPE_I;
            }
            callback(packet);
        }
    }

    bool CreateEncoder(const std::string& codec_name, int bitrate) {
        codec = codec_name;
        const bool hevc = codec == "hevc";
        // AMF names its H.264 component VideoEncoderVCE (AVC); there is no
        // VideoEncoderH264 component in the public headers.
        const wchar_t* component = hevc ? AMFVideoEncoder_HEVC
                                        : AMFVideoEncoderVCE_AVC;
        AMF_RESULT result = factory->CreateComponent(context, component, &encoder);
        if (result != AMF_OK) {
            LogAmfError("CreateComponent", result);
            return false;
        }

        if (hevc) {
            encoder->SetProperty(AMF_VIDEO_ENCODER_HEVC_USAGE,
                                 AMF_VIDEO_ENCODER_HEVC_USAGE_TRANSCODING);
            encoder->SetProperty(AMF_VIDEO_ENCODER_HEVC_TARGET_BITRATE,
                                 static_cast<amf_int64>(bitrate));
            encoder->SetProperty(AMF_VIDEO_ENCODER_HEVC_RATE_CONTROL_METHOD,
                                 AMF_VIDEO_ENCODER_HEVC_RATE_CONTROL_METHOD_CBR);
            encoder->SetProperty(AMF_VIDEO_ENCODER_HEVC_FRAMERATE,
                                 AMFConstructRate(fps, 1));
            // HEVC has no IDRPeriod property; GOP size controls the IDR cadence.
            encoder->SetProperty(AMF_VIDEO_ENCODER_HEVC_GOP_SIZE,
                                 static_cast<amf_int64>(fps * 2));
            encoder->SetProperty(AMF_VIDEO_ENCODER_HEVC_PROFILE,
                                 AMF_VIDEO_ENCODER_HEVC_PROFILE_MAIN);
            encoder->SetProperty(AMF_VIDEO_ENCODER_HEVC_QUALITY_PRESET,
                                 AMF_VIDEO_ENCODER_HEVC_QUALITY_PRESET_SPEED);
        } else {
            encoder->SetProperty(AMF_VIDEO_ENCODER_USAGE,
                                 AMF_VIDEO_ENCODER_USAGE_TRANSCODING);
            encoder->SetProperty(AMF_VIDEO_ENCODER_TARGET_BITRATE,
                                 static_cast<amf_int64>(bitrate));
            encoder->SetProperty(AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD,
                                 AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD_CBR);
            encoder->SetProperty(AMF_VIDEO_ENCODER_FRAMERATE,
                                 AMFConstructRate(fps, 1));
            encoder->SetProperty(AMF_VIDEO_ENCODER_IDR_PERIOD,
                                 static_cast<amf_int64>(fps * 2));
            encoder->SetProperty(AMF_VIDEO_ENCODER_PROFILE,
                                 AMF_VIDEO_ENCODER_PROFILE_HIGH);
            encoder->SetProperty(AMF_VIDEO_ENCODER_QUALITY_PRESET,
                                 AMF_VIDEO_ENCODER_QUALITY_PRESET_SPEED);
        }

        result = encoder->Init(amf::AMF_SURFACE_NV12, width, height);
        if (result != AMF_OK) {
            LogAmfError("Init", result);
            return false;
        }
        return true;
    }
};

AmfEncoder::~AmfEncoder() {
    Shutdown();
}

bool AmfEncoder::Initialize(ID3D11Device* device, const std::string& codec,
                            int width, int height, int fps, int bitrate) {
    Shutdown();
    impl_ = new Impl();
    impl_->width = width;
    impl_->height = height;
    impl_->fps = fps;

    impl_->module = LoadLibraryExW(L"amfrt64.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!impl_->module) {
        impl_->module = LoadLibraryW(L"amfrt64.dll");
    }
    if (!impl_->module) {
        std::fprintf(stderr, "Failed to load amfrt64.dll\n");
        Shutdown();
        return false;
    }

    auto init = reinterpret_cast<AMFInit_Fn>(
        GetProcAddress(impl_->module, AMF_INIT_FUNCTION_NAME));
    if (!init) {
        std::fprintf(stderr, "AMFInit not found\n");
        Shutdown();
        return false;
    }

    AMF_RESULT result = init(AMF_FULL_VERSION, &impl_->factory);
    if (result != AMF_OK || !impl_->factory) {
        LogAmfError("AMFInit", result);
        Shutdown();
        return false;
    }

    result = impl_->factory->CreateContext(&impl_->context);
    if (result != AMF_OK) {
        LogAmfError("CreateContext", result);
        Shutdown();
        return false;
    }

    result = impl_->context->InitDX11(device);
    if (result != AMF_OK) {
        LogAmfError("InitDX11", result);
        Shutdown();
        return false;
    }

    if (!impl_->CreateEncoder(codec, bitrate)) {
        Shutdown();
        return false;
    }
    return true;
}

bool AmfEncoder::Encode(ID3D11Texture2D* nv12_texture, int64_t pts_us,
                        const PacketCallback& callback) {
    if (!impl_ || !impl_->encoder || !nv12_texture) {
        return false;
    }

    amf::AMFSurfacePtr surface;
    AMF_RESULT result = impl_->context->CreateSurfaceFromDX11Native(
        nv12_texture, &surface, nullptr);
    if (result != AMF_OK || !surface) {
        LogAmfError("CreateSurfaceFromDX11Native", result);
        return false;
    }

    surface->SetPts(pts_us * kAmfPtsPerUs);
    // The hardware encoder has a bounded input queue. AMF_INPUT_FULL is a
    // back-pressure signal: drain completed frames, then resubmit. A one
    // millisecond wait keeps the polling loop from spinning while the GPU
    // catches up.
    constexpr int kMaxSubmitAttempts = 1000;
    for (int attempt = 0; attempt < kMaxSubmitAttempts; ++attempt) {
        result = impl_->encoder->SubmitInput(surface);
        if (result != AMF_INPUT_FULL) {
            break;
        }
        if (!impl_->Drain(callback)) {
            return false;
        }
        Sleep(1);
    }
    if (result != AMF_OK) {
        LogAmfError("SubmitInput", result);
        return false;
    }
    return impl_->Drain(callback);
}

bool AmfEncoder::Flush(const PacketCallback& callback) {
    if (!impl_ || !impl_->encoder) {
        return false;
    }
    AMF_RESULT result = impl_->encoder->Drain();
    if (result != AMF_OK) {
        LogAmfError("Drain", result);
    }
    return impl_->Drain(callback);
}

void AmfEncoder::Shutdown() {
    if (!impl_) {
        return;
    }
    if (impl_->encoder) {
        impl_->encoder->Terminate();
        impl_->encoder = nullptr;
    }
    if (impl_->context) {
        impl_->context->Terminate();
        impl_->context = nullptr;
    }
    impl_->factory = nullptr;
    if (impl_->module) {
        FreeLibrary(impl_->module);
        impl_->module = nullptr;
    }
    delete impl_;
    impl_ = nullptr;
}

bool AmfEncoder::is_initialized() const {
    return impl_ && impl_->encoder;
}

}  // namespace hmmob
