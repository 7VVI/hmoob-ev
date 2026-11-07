#include "core/encoder/AmfEncoder.h"

#include <windows.h>
#include <wrl/client.h>
#include <core/Buffer.h>
#include <core/Context.h>
#include <core/Factory.h>
#include <core/Surface.h>
#include <components/VideoEncoderHEVC.h>
#include <components/VideoEncoderVCE.h>
#include <algorithm>
#include <chrono>
#include <cstdio>

namespace hmmob {

namespace {
// AMF PTS is expressed in 100-nanosecond units.
constexpr int64_t kAmfPtsPerUs = 10;
// The hardware encoder queue is shallow; a 60 fps capture thread must not
// block for long, so bound the AMF_INPUT_FULL retry window.
constexpr int kMaxSubmitAttempts = 16;
constexpr auto kFlushTimeout = std::chrono::seconds(2);

void LogAmfError(const char* what, AMF_RESULT result) {
    std::fprintf(stderr, "AMF %s failed: %d\n", what, static_cast<int>(result));
}

// Returns the NAL unit types present in an Annex B elementary stream.
std::vector<int> AnnexBNalTypes(const std::vector<uint8_t>& data, bool hevc) {
    std::vector<int> types;
    const size_t size = data.size();
    for (size_t i = 0; i + 2 < size; ++i) {
        size_t pos = 0;
        if (data[i] == 0 && data[i + 1] == 0 && data[i + 2] == 1) {
            pos = i + 3;
        } else if (i + 3 < size && data[i] == 0 && data[i + 1] == 0 &&
                   data[i + 2] == 0 && data[i + 3] == 1) {
            pos = i + 4;
        } else {
            continue;
        }
        if (pos >= size) {
            continue;
        }
        types.push_back(hevc ? ((data[pos] >> 1) & 0x3F) : (data[pos] & 0x1F));
        i = pos - 1;
    }
    return types;
}

bool HasKeyframeNal(const std::vector<uint8_t>& data, bool hevc) {
    const std::vector<int> types = AnnexBNalTypes(data, hevc);
    if (hevc) {
        return std::find_if(types.begin(), types.end(), [](int type) {
                   return type == 19 || type == 20 || type == 21;
               }) != types.end();
    }
    return std::find(types.begin(), types.end(), 5) != types.end();
}
}  // namespace

struct AmfEncoder::Impl {
    enum class QueryStatus { Packet, NoOutput, Eof, Error };

    HMODULE module = nullptr;
    amf::AMFFactory* factory = nullptr;
    amf::AMFContextPtr context;
    amf::AMFComponentPtr encoder;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> d3d_context;
    int width = 0;
    int height = 0;
    int fps = 0;
    std::string codec;

    [[nodiscard]] bool IsHevc() const { return codec == "hevc"; }

    template <typename T>
    [[nodiscard]] bool SetPropertyChecked(const char* label, const wchar_t* name,
                                          const T& value) {
        const AMF_RESULT result = encoder->SetProperty(name, value);
        if (result != AMF_OK) {
            std::fprintf(stderr, "AMF property %s failed: %d\n", label,
                         static_cast<int>(result));
            return false;
        }
        return true;
    }

    QueryStatus QueryOne(const PacketCallback& callback) {
        amf::AMFDataPtr data;
        AMF_RESULT result = encoder->QueryOutput(&data);
        if (result == AMF_EOF) {
            return QueryStatus::Eof;
        }
        // AMF_REPEAT is the normal "nothing ready yet" result.
        if (result == AMF_REPEAT) {
            return QueryStatus::NoOutput;
        }
        if (result != AMF_OK) {
            LogAmfError("QueryOutput", result);
            return QueryStatus::Error;
        }
        if (!data) {
            return QueryStatus::NoOutput;
        }

        amf::AMFBufferPtr buffer(data);
        if (!buffer) {
            std::fprintf(stderr, "AMF QueryOutput: output is not a buffer\n");
            return QueryStatus::Error;
        }

        AmfPacket packet;
        packet.pts_us = data->GetPts() / kAmfPtsPerUs;
        const auto* bytes = static_cast<const uint8_t*>(buffer->GetNative());
        const amf_size size = buffer->GetSize();
        if (!bytes || size == 0) {
            std::fprintf(stderr, "AMF QueryOutput: empty bitstream buffer\n");
            return QueryStatus::Error;
        }
        packet.data.assign(bytes, bytes + size);

        amf_int64 type = 0;
        const wchar_t* type_property = IsHevc() ? AMF_VIDEO_ENCODER_HEVC_OUTPUT_DATA_TYPE
                                                : AMF_VIDEO_ENCODER_OUTPUT_DATA_TYPE;
        if (data->GetProperty(type_property, &type) == AMF_OK) {
            // Only IDR starts a new keyframe; an I-frame is not a random-access point.
            packet.keyframe =
                IsHevc() ? (type == AMF_VIDEO_ENCODER_HEVC_OUTPUT_DATA_TYPE_IDR)
                         : (type == AMF_VIDEO_ENCODER_OUTPUT_DATA_TYPE_IDR);
        } else {
            packet.keyframe = HasKeyframeNal(packet.data, IsHevc());
        }
        callback(packet);
        return QueryStatus::Packet;
    }

    // Drains everything currently available without waiting.
    [[nodiscard]] bool Drain(const PacketCallback& callback) {
        for (;;) {
            switch (QueryOne(callback)) {
                case QueryStatus::Packet:
                    continue;
                case QueryStatus::NoOutput:
                case QueryStatus::Eof:
                    return true;
                case QueryStatus::Error:
                    return false;
            }
        }
    }

    // Waits until AMF reports end-of-stream. Used after encoder->Drain().
    [[nodiscard]] bool DrainToEof(const PacketCallback& callback) {
        const auto deadline = std::chrono::steady_clock::now() + kFlushTimeout;
        for (;;) {
            switch (QueryOne(callback)) {
                case QueryStatus::Packet:
                    continue;
                case QueryStatus::Eof:
                    return true;
                case QueryStatus::Error:
                    return false;
                case QueryStatus::NoOutput:
                    if (std::chrono::steady_clock::now() >= deadline) {
                        std::fprintf(stderr, "AMF Flush timed out waiting for EOF\n");
                        return false;
                    }
                    Sleep(1);
                    break;
            }
        }
    }

    [[nodiscard]] bool CreateEncoder(int bitrate) {
        const bool hevc = IsHevc();
        // AMF names its H.264 component VideoEncoderVCE (AVC); there is no
        // VideoEncoderH264 component in the public headers.
        const wchar_t* component = hevc ? AMFVideoEncoder_HEVC
                                        : AMFVideoEncoderVCE_AVC;
        const AMF_RESULT result = factory->CreateComponent(context, component, &encoder);
        if (result != AMF_OK) {
            LogAmfError("CreateComponent", result);
            return false;
        }

        const amf_int64 gop = static_cast<amf_int64>(static_cast<int64_t>(fps) * 2);
        if (hevc) {
            // Usage must be set first: it fully configures the parameter set.
            if (!SetPropertyChecked("HevcUsage", AMF_VIDEO_ENCODER_HEVC_USAGE,
                                    AMF_VIDEO_ENCODER_HEVC_USAGE_LOW_LATENCY) ||
                !SetPropertyChecked("HevcTargetBitrate",
                                    AMF_VIDEO_ENCODER_HEVC_TARGET_BITRATE,
                                    static_cast<amf_int64>(bitrate)) ||
                !SetPropertyChecked("HevcRateControlMethod",
                                    AMF_VIDEO_ENCODER_HEVC_RATE_CONTROL_METHOD,
                                    AMF_VIDEO_ENCODER_HEVC_RATE_CONTROL_METHOD_CBR) ||
                !SetPropertyChecked("HevcFrameRate", AMF_VIDEO_ENCODER_HEVC_FRAMERATE,
                                    AMFConstructRate(fps, 1)) ||
                // HEVC has no IDRPeriod property; GOP size controls IDR cadence.
                !SetPropertyChecked("HevcGOPSize", AMF_VIDEO_ENCODER_HEVC_GOP_SIZE, gop) ||
                !SetPropertyChecked("HevcInputQueueSize",
                                    AMF_VIDEO_ENCODER_HEVC_INPUT_QUEUE_SIZE,
                                    static_cast<amf_int64>(4)) ||
                !SetPropertyChecked("HevcProfile", AMF_VIDEO_ENCODER_HEVC_PROFILE,
                                    AMF_VIDEO_ENCODER_HEVC_PROFILE_MAIN) ||
                !SetPropertyChecked("HevcQualityPreset",
                                    AMF_VIDEO_ENCODER_HEVC_QUALITY_PRESET,
                                    AMF_VIDEO_ENCODER_HEVC_QUALITY_PRESET_SPEED)) {
                return false;
            }
        } else {
            if (!SetPropertyChecked("Usage", AMF_VIDEO_ENCODER_USAGE,
                                    AMF_VIDEO_ENCODER_USAGE_LOW_LATENCY) ||
                !SetPropertyChecked("TargetBitrate", AMF_VIDEO_ENCODER_TARGET_BITRATE,
                                    static_cast<amf_int64>(bitrate)) ||
                !SetPropertyChecked("RateControlMethod",
                                    AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD,
                                    AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD_CBR) ||
                !SetPropertyChecked("FrameRate", AMF_VIDEO_ENCODER_FRAMERATE,
                                    AMFConstructRate(fps, 1)) ||
                !SetPropertyChecked("IDRPeriod", AMF_VIDEO_ENCODER_IDR_PERIOD, gop) ||
                !SetPropertyChecked("InputQueueSize",
                                    AMF_VIDEO_ENCODER_INPUT_QUEUE_SIZE,
                                    static_cast<amf_int64>(4)) ||
                // No B-frames keeps output in presentation order so Task 8 can
                // use dts == pts.
                !SetPropertyChecked("BPicturesPattern",
                                    AMF_VIDEO_ENCODER_B_PIC_PATTERN,
                                    static_cast<amf_int64>(0)) ||
                !SetPropertyChecked("MaxConsecutiveBPictures",
                                    AMF_VIDEO_ENCODER_MAX_CONSECUTIVE_BPICTURES,
                                    static_cast<amf_int64>(0)) ||
                !SetPropertyChecked("Profile", AMF_VIDEO_ENCODER_PROFILE,
                                    AMF_VIDEO_ENCODER_PROFILE_HIGH) ||
                !SetPropertyChecked("QualityPreset", AMF_VIDEO_ENCODER_QUALITY_PRESET,
                                    AMF_VIDEO_ENCODER_QUALITY_PRESET_SPEED)) {
                return false;
            }
        }

        const AMF_RESULT init_result =
            encoder->Init(amf::AMF_SURFACE_NV12, width, height);
        if (init_result != AMF_OK) {
            LogAmfError("Init", init_result);
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
    if (!device) {
        std::fprintf(stderr, "AMF Initialize: device is null\n");
        return false;
    }
    if (codec != "hevc" && codec != "h264") {
        std::fprintf(stderr, "AMF Initialize: unsupported codec '%s'\n", codec.c_str());
        return false;
    }
    if (width <= 0 || height <= 0 || fps <= 0 || bitrate <= 0) {
        std::fprintf(stderr,
                     "AMF Initialize: invalid dimensions or rate (%dx%d, %d fps, %d bps)\n",
                     width, height, fps, bitrate);
        return false;
    }
    if ((width % 2) != 0 || (height % 2) != 0) {
        std::fprintf(stderr, "AMF Initialize: NV12 requires even dimensions (%dx%d)\n",
                     width, height);
        return false;
    }

    Shutdown();
    impl_ = new Impl();
    impl_->width = width;
    impl_->height = height;
    impl_->fps = fps;
    impl_->codec = codec;

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

    device->GetImmediateContext(&impl_->d3d_context);
    if (!impl_->d3d_context) {
        std::fprintf(stderr, "AMF Initialize: no immediate D3D11 context\n");
        Shutdown();
        return false;
    }

    if (!impl_->CreateEncoder(bitrate)) {
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

    D3D11_TEXTURE2D_DESC src_desc{};
    nv12_texture->GetDesc(&src_desc);
    if (src_desc.Format != DXGI_FORMAT_NV12 ||
        src_desc.Width != static_cast<UINT>(impl_->width) ||
        src_desc.Height != static_cast<UINT>(impl_->height) ||
        src_desc.ArraySize != 1) {
        std::fprintf(stderr, "AMF Encode: input must be NV12, %dx%d, ArraySize=1\n",
                     impl_->width, impl_->height);
        return false;
    }

    // AMF owns the encoded surface. The caller's texture is copied on the GPU
    // and may be reused immediately for the next captured frame.
    amf::AMFSurfacePtr surface;
    AMF_RESULT result = impl_->context->AllocSurface(
        amf::AMF_MEMORY_DX11, amf::AMF_SURFACE_NV12, impl_->width, impl_->height,
        &surface);
    if (result != AMF_OK || !surface) {
        LogAmfError("AllocSurface", result);
        return false;
    }

    amf::AMFPlane* plane = surface->GetPlane(amf::AMF_PLANE_PACKED);
    if (!plane || !plane->GetNative()) {
        std::fprintf(stderr, "AMF surface has no native texture\n");
        return false;
    }
    auto* dst = static_cast<ID3D11Texture2D*>(plane->GetNative());
    impl_->d3d_context->CopyResource(dst, nv12_texture);
    surface->SetPts(pts_us * kAmfPtsPerUs);

    int attempt = 0;
    for (; attempt < kMaxSubmitAttempts; ++attempt) {
        result = impl_->encoder->SubmitInput(surface);
        // AMF_NEED_MORE_INPUT means the frame was accepted but no output was
        // produced yet.
        if (result == AMF_OK || result == AMF_NEED_MORE_INPUT) {
            break;
        }
        if (result != AMF_INPUT_FULL) {
            LogAmfError("SubmitInput", result);
            return false;
        }
        if (!impl_->Drain(callback)) {
            return false;
        }
        Sleep(1);
    }
    if (attempt == kMaxSubmitAttempts) {
        std::fprintf(stderr, "AMF Encode: input queue remained full\n");
        return false;
    }
    return impl_->Drain(callback);
}

bool AmfEncoder::Flush(const PacketCallback& callback) {
    if (!impl_ || !impl_->encoder) {
        return false;
    }
    const AMF_RESULT result = impl_->encoder->Drain();
    if (result != AMF_OK) {
        LogAmfError("Drain", result);
        return false;
    }
    return impl_->DrainToEof(callback);
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
    impl_->d3d_context.Reset();
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
