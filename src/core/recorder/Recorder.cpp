#include "core/recorder/Recorder.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <exception>
#include <future>
#include <objbase.h>
#include <utility>

#include "core/audio/AacEncoder.h"
#include "core/audio/AudioMixer.h"
#include "core/audio/WasapiCapture.h"
#include "core/capture/DesktopSource.h"
#include "core/capture/VideoSource.h"
#include "core/capture/WindowSource.h"
#include "core/common/QpcClock.h"
#include "core/encoder/AmfEncoder.h"
#include "core/gfx/D3D11Device.h"
#include "core/gfx/HdrToneMapper.h"
#include "core/gfx/VideoProcessor.h"
#include "core/mux/FfmpegMuxer.h"

namespace hmmob {
namespace {

// Resolves the Start() handshake even if Run() unwinds through an exception.
class InitPublisher {
public:
    explicit InitPublisher(std::promise<bool>& promise) : promise_(promise) {}
    InitPublisher(const InitPublisher&) = delete;
    InitPublisher& operator=(const InitPublisher&) = delete;

    ~InitPublisher() {
        if (!resolved_) {
            try {
                promise_.set_value(false);
            } catch (...) {
            }
        }
    }

    void Resolve(bool ok) {
        promise_.set_value(ok);
        resolved_ = true;
    }

private:
    std::promise<bool>& promise_;
    bool resolved_ = false;
};

void ReportWarning(const char* message) { std::fprintf(stderr, "%s\n", message); }

std::unique_ptr<VideoSource> CreateVideoSource(const RecorderConfig& config,
                                               std::string& error) {
    switch (config.mode) {
        case CaptureMode::Screen:
        case CaptureMode::Region:
            return std::make_unique<DesktopSource>();
        case CaptureMode::Window:
            if (!config.window) {
                error = "no window selected";
                return nullptr;
            }
            return std::make_unique<WindowSource>();
        case CaptureMode::Camera:
            error = "camera capture is not implemented yet";
            return nullptr;
    }
    error = "unknown capture mode";
    return nullptr;
}

}  // namespace

struct Recorder::Impl {
    RecorderConfig config;
    std::thread thread;
    std::promise<bool> init_promise;

    mutable std::mutex mutex;
    std::string error;
    RecorderStats stats;

    std::atomic<bool> stop_requested{false};
    std::atomic<bool> running{false};
    std::atomic<bool> finished{false};

    // Run state, written only by the recording thread and published to
    // `stats` under the mutex. Kept here so the thread-level catch handler can
    // publish the latest values if the pipeline throws.
    int64_t start_us = 0;
    int acquired = 0;
    int new_desktop_frames = 0;
    int encoded_frames = 0;
    int packets_written = 0;
    int encode_errors = 0;
    int64_t encoded_bytes = 0;
    int audio_packets = 0;
    int64_t audio_bytes = 0;
    int64_t target_frames = 0;
    bool audio_track = false;

    // Preview state. The converter and staging texture are owned by the
    // recording thread; `latest_preview` is published under `mutex` so the UI
    // can pull it from another thread.
    VideoProcessor preview_converter;
    ComPtr<ID3D11Texture2D> preview_staging;
    std::shared_ptr<const PreviewFrame> latest_preview;
    uint64_t preview_sequence = 0;
    int preview_interval = 1;
    bool preview_enabled = false;
    bool preview_error_logged = false;

    void Run();
    void PublishStats(int64_t now_us);
    void ResetRunState();
    void PublishPreview(ID3D11DeviceContext* context, ID3D11Texture2D* source,
                        const RECT& source_rect);

    // Keeps the first failure: later cleanup failures are consequences of it.
    void RecordError(std::string message) {
        std::lock_guard<std::mutex> lock(mutex);
        if (error.empty()) {
            error = std::move(message);
        }
    }

    bool HasError() const {
        std::lock_guard<std::mutex> lock(mutex);
        return !error.empty();
    }

    void MarkFinished() {
        running.store(false);
        finished.store(true);
    }
};

void Recorder::Impl::ResetRunState() {
    start_us = 0;
    acquired = 0;
    new_desktop_frames = 0;
    encoded_frames = 0;
    packets_written = 0;
    encode_errors = 0;
    encoded_bytes = 0;
    audio_packets = 0;
    audio_bytes = 0;
    target_frames = 0;
    audio_track = false;
}

void Recorder::Impl::PublishStats(int64_t now_us) {
    std::lock_guard<std::mutex> lock(mutex);
    stats.elapsed_us = start_us != 0 ? now_us - start_us : 0;
    stats.acquired = acquired;
    stats.new_desktop_frames = new_desktop_frames;
    stats.encoded_frames = encoded_frames;
    stats.packets_written = packets_written;
    stats.encoded_bytes = encoded_bytes;
    stats.encode_errors = encode_errors;
    stats.audio_packets = audio_packets;
    stats.audio_bytes = audio_bytes;
    stats.target_frames = static_cast<int>(target_frames);
    stats.audio_track = audio_track;
}

void Recorder::Impl::PublishPreview(ID3D11DeviceContext* context,
                                    ID3D11Texture2D* source,
                                    const RECT& source_rect) {
    if (!context || !source || !preview_staging ||
        !preview_converter.output_texture()) {
        return;
    }

    const auto log_failure = [this](const char* stage, HRESULT hr) {
        if (!preview_error_logged) {
            std::fprintf(stderr,
                         "warning: preview %s failed (0x%08lx); "
                         "preview updates stopped\n",
                         stage, static_cast<unsigned long>(hr));
            preview_error_logged = true;
        }
    };

    // Downscale to the preview size on the GPU. The result is a BGRA8 texture
    // so the readback rows can be handed to the UI as-is.
    if (!preview_converter.Convert(source,
                                   preview_converter.output_texture(),
                                   source_rect)) {
        log_failure("conversion", E_FAIL);
        return;
    }
    context->CopyResource(preview_staging.Get(),
                          preview_converter.output_texture());

    // Allocate the frame before mapping: an allocation failure unwinds here
    // with the staging texture still unmapped, and only the noexcept row copy
    // runs while the texture is mapped.
    auto preview = std::make_shared<PreviewFrame>();
    preview->width = config.preview_width;
    preview->height = config.preview_height;
    preview->stride = config.preview_width * 4;
    preview->bgra.resize(static_cast<size_t>(preview->stride) *
                         static_cast<size_t>(preview->height));

    D3D11_MAPPED_SUBRESOURCE mapped{};
    const HRESULT map_hr =
        context->Map(preview_staging.Get(), 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(map_hr)) {
        // Nothing was mapped, so there is nothing to unmap.
        log_failure("readback", map_hr);
        return;
    }

    const size_t row_bytes = static_cast<size_t>(preview->stride);
    for (int y = 0; y < preview->height; ++y) {
        std::memcpy(preview->bgra.data() + static_cast<size_t>(y) * row_bytes,
                    static_cast<const uint8_t*>(mapped.pData) +
                        static_cast<size_t>(y) * mapped.RowPitch,
                    row_bytes);
    }
    context->Unmap(preview_staging.Get(), 0);
    preview->sequence = ++preview_sequence;
    {
        std::lock_guard<std::mutex> lock(mutex);
        latest_preview = std::move(preview);
    }
}

void Recorder::Impl::Run() {
    // The recording thread owns the D3D/WASAPI objects and needs COM for the
    // audio endpoints. The guard destroys after the pipeline locals, so
    // CoUninitialize runs only once they are gone.
    const HRESULT com_result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    struct ComScope {
        HRESULT result;
        ~ComScope() {
            if (SUCCEEDED(result)) {
                CoUninitialize();
            }
        }
    } com_scope{com_result};
    (void)com_scope;

    InitPublisher init(init_promise);

    const auto fail_init = [&](std::string message) {
        RecordError(std::move(message));
        MarkFinished();
        init.Resolve(false);
    };

    D3D11Device d3d;
    if (!d3d.Create()) {
        fail_init("failed to create d3d11 device");
        return;
    }

    const auto displays = d3d.EnumerateDisplays();
    if (displays.empty()) {
        fail_init("no display found");
        return;
    }
    if (config.display_index < 0 ||
        config.display_index >= static_cast<int>(displays.size())) {
        fail_init("display index out of range");
        return;
    }

    std::string source_error;
    const std::unique_ptr<VideoSource> source =
        CreateVideoSource(config, source_error);
    if (!source) {
        fail_init(source_error);
        return;
    }
    if (!source->Initialize(d3d, config, source_error)) {
        fail_init(source_error.empty() ? "failed to initialize capture source"
                                       : source_error);
        return;
    }
    if (source->hdr_downgraded()) {
        ReportWarning(
            "warning: HDR capture was requested but the display is SDR; "
            "recording SDR");
    }

    // Encoded size (the region in Region mode) and the size of the texture the
    // source actually delivers.
    const int width = source->width();
    const int height = source->height();
    const int texture_width = source->texture_width();
    const int texture_height = source->texture_height();
    const DXGI_FORMAT format = source->format();
    {
        std::lock_guard<std::mutex> lock(mutex);
        stats.display_width = width;
        stats.display_height = height;
        stats.display_format = static_cast<int>(format);
    }

    // PQ vs scRGB cannot be inferred from the surface format alone: HDR10
    // streams PQ while scRGB (RGB_FULL_G10_NONE_P709) is already linear.
    const bool hdr_display = IsHdrColorSpace(source->color_space());
    const bool pq_mode =
        source->color_space() == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020 ||
        source->color_space() == DXGI_COLOR_SPACE_RGB_STUDIO_G2084_NONE_P2020;
    // Don't compile shaders or allocate a full-resolution render target when
    // capture is SDR.
    const bool tone_map_hdr = source->hdr_capable() && hdr_display;
    HdrToneMapper tone_mapper;
    if (tone_map_hdr &&
        !tone_mapper.Initialize(d3d, texture_width, texture_height, pq_mode)) {
        fail_init("failed to initialize HDR tone mapper");
        return;
    }

    // Input is the full captured texture; output is the encoded size, which
    // differs from the input in Region mode.
    VideoProcessor converter;
    if (!converter.Initialize(d3d, texture_width, texture_height, width, height,
                              DXGI_FORMAT_NV12)) {
        fail_init("failed to initialize video processor");
        return;
    }

    AmfEncoder encoder;
    if (!encoder.Initialize(d3d.device(), config.codec, width, height,
                            config.fps, static_cast<int>(config.bitrate))) {
        fail_init("failed to initialize AMF encoder");
        return;
    }

    constexpr int kAudioSampleRate = 48000;
    constexpr int kAudioChannels = 2;
    constexpr int kAudioBitrate = 192000;

    WasapiCapture system_capture;
    WasapiCapture mic_capture;
    bool audio_enabled = config.system_audio || config.microphone;
    bool system_ok = false;
    bool mic_ok = false;

    if (config.system_audio) {
        system_ok = system_capture.Initialize(true, kAudioSampleRate,
                                              kAudioChannels) &&
                    system_capture.Start();
        if (!system_ok) {
            std::fprintf(stderr, "warning: system audio unavailable (0x%08lx)\n",
                         static_cast<unsigned long>(system_capture.last_error()));
        }
    }
    if (config.microphone) {
        mic_ok = mic_capture.Initialize(false, kAudioSampleRate,
                                        kAudioChannels) &&
                 mic_capture.Start();
        if (!mic_ok) {
            std::fprintf(stderr, "warning: microphone unavailable (0x%08lx)\n",
                         static_cast<unsigned long>(mic_capture.last_error()));
        }
    }
    audio_enabled = audio_enabled && (system_ok || mic_ok);

    AudioMixer audio_mixer(kAudioSampleRate, kAudioChannels);
    if (system_ok) audio_mixer.SetSystemSource(system_capture.ring());
    if (mic_ok) audio_mixer.SetMicSource(mic_capture.ring());
    audio_mixer.SetSystemGain(config.system_gain);
    audio_mixer.SetMicGain(config.mic_gain);

    AacEncoder aac;
    if (audio_enabled &&
        !aac.Initialize(kAudioSampleRate, kAudioChannels, kAudioBitrate)) {
        ReportWarning("warning: AAC encoder unavailable; recording video only");
        audio_enabled = false;
        system_capture.Stop();
        mic_capture.Stop();
        system_ok = mic_ok = false;
        audio_mixer.SetSystemSource(nullptr);
        audio_mixer.SetMicSource(nullptr);
    }

    AudioConfig audio_config;
    audio_config.enabled = audio_enabled;
    if (audio_enabled) {
        audio_config.sample_rate = aac.sample_rate();
        audio_config.channels = aac.channels();
        audio_config.bitrate = aac.bitrate();
        audio_config.frame_size = aac.frame_size();
        audio_config.initial_padding = aac.initial_padding_samples();
        audio_config.extradata = aac.extradata();
    }

    FfmpegMuxer muxer;
    if (!muxer.Open(config.output_path, config.codec, width, height, config.fps,
                    static_cast<int>(config.bitrate), audio_config)) {
        fail_init("failed to open output " + config.output_path);
        return;
    }

    // Both captures have been filling their rings while the microphone, the
    // AAC encoder and the muxer were initialized. Drop that pre-roll so the
    // track starts at frame 0 and both sources stay aligned with each other.
    if (system_ok) system_capture.ring()->Clear();
    if (mic_ok) mic_capture.ring()->Clear();
    audio_track = audio_enabled;

    // Optional preview pipeline: downscale the latest frame to a small BGRA8
    // texture for the UI. A deliberate disable (any value <= 0) stays silent;
    // a requested preview that cannot be set up warns once with the failing
    // HRESULT and never fails the recording.
    const bool preview_requested =
        config.preview_width > 0 && config.preview_height > 0 &&
        config.preview_fps > 0;
    if (preview_requested) {
        preview_interval = std::max(1, config.fps / config.preview_fps);
        if (!preview_converter.Initialize(d3d, texture_width, texture_height,
                                          config.preview_width,
                                          config.preview_height,
                                          DXGI_FORMAT_B8G8R8A8_UNORM)) {
            std::fprintf(stderr,
                         "warning: preview disabled (VideoProcessor "
                         "Initialize failed: 0x%08lx)\n",
                         static_cast<unsigned long>(
                             preview_converter.last_error()));
        } else {
            D3D11_TEXTURE2D_DESC staging_desc{};
            staging_desc.Width = static_cast<UINT>(config.preview_width);
            staging_desc.Height = static_cast<UINT>(config.preview_height);
            staging_desc.MipLevels = 1;
            staging_desc.ArraySize = 1;
            staging_desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
            staging_desc.SampleDesc.Count = 1;
            staging_desc.Usage = D3D11_USAGE_STAGING;
            staging_desc.BindFlags = 0;
            staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            const HRESULT staging_hr = d3d.device()->CreateTexture2D(
                &staging_desc, nullptr, &preview_staging);
            preview_enabled = SUCCEEDED(staging_hr);
            if (!preview_enabled) {
                std::fprintf(stderr,
                             "warning: preview disabled (CreateTexture2D "
                             "failed: 0x%08lx)\n",
                             static_cast<unsigned long>(staging_hr));
            }
        }
    }

    QpcClock clock;
    start_us = clock.NowUs();
    const int64_t frame_interval_us = 1'000'000 / config.fps;
    target_frames = static_cast<int64_t>(config.duration_seconds) * config.fps;
    int64_t next_deadline_us = start_us;
    bool have_frame = false;
    bool write_failed = false;
    // Latest BGRA source handed to the encoder path; reused for previews on
    // iterations where no new desktop frame arrived. The rectangle is the
    // encoded region inside that texture.
    ID3D11Texture2D* last_source = nullptr;
    RECT last_source_rect{};

    const auto fail = [&](std::string message) {
        RecordError(std::move(message));
    };

    const auto on_packet = [&](const AmfPacket& packet) {
        if (!muxer.WritePacket(packet.data.data(), packet.data.size(),
                               packet.pts_us, packet.keyframe)) {
            write_failed = true;
            return;
        }
        ++packets_written;
        encoded_bytes += static_cast<int64_t>(packet.data.size());
    };

    int64_t audio_samples_written = 0;
    bool audio_encode_failed = false;
    std::vector<float> audio_buffer;
    const auto on_audio_packet = [&](const AacPacket& packet) {
        const int64_t pts_us =
            packet.pts_samples * 1'000'000 / kAudioSampleRate;
        if (!muxer.WriteAudioPacket(packet.data.data(), packet.data.size(),
                                    pts_us, packet.duration_samples)) {
            write_failed = true;
            return;
        }
        ++audio_packets;
        audio_bytes += static_cast<int64_t>(packet.data.size());
    };

    // Publish once before the loop so target_frames/audio_track (and
    // elapsed=0) are visible as soon as the caller sees a running recorder.
    PublishStats(start_us);
    running.store(true);
    init.Resolve(true);

    for (;;) {
        // Checked every iteration (including while pacing) so RequestStop()
        // takes effect within one frame without aborting a frame mid-encode.
        if (stop_requested.load()) {
            break;
        }
        const int64_t now_us = clock.NowUs();
        if (config.duration_seconds > 0 &&
            now_us - start_us >=
                static_cast<int64_t>(config.duration_seconds) * 1'000'000) {
            break;
        }
        if (now_us < next_deadline_us) {
            const int64_t sleep_us =
                std::min<int64_t>(next_deadline_us - now_us, 1000);
            std::this_thread::sleep_for(std::chrono::microseconds(sleep_us));
            continue;
        }
        SourceFrame frame;
        if (source->Acquire(frame, 0)) {
            if (frame.width != width || frame.height != height ||
                frame.format != format) {
                char message[128];
                std::snprintf(message, sizeof(message),
                              "display mode changed: %dx%d fmt %d -> %dx%d fmt %d",
                              width, height, static_cast<int>(format),
                              frame.width, frame.height,
                              static_cast<int>(frame.format));
                fail(message);
                break;
            }
            ID3D11Texture2D* encoded_source = frame.texture;
            if (tone_map_hdr) {
                encoded_source = tone_mapper.Map(frame.texture);
                if (!encoded_source) {
                    fail("HDR tone mapping failed");
                    break;
                }
            } else if (frame.hdr) {
                char message[160];
                std::snprintf(
                    message, sizeof(message),
                    "HDR capture format %d with SDR color space %d is "
                    "not supported in Phase 0",
                    static_cast<int>(frame.format),
                    static_cast<int>(source->color_space()));
                fail(message);
                break;
            }
            if (!converter.Convert(encoded_source, converter.output_texture(),
                                   frame.source_rect)) {
                fail("color conversion failed");
                break;
            }
            have_frame = true;
            last_source = encoded_source;
            last_source_rect = frame.source_rect;
            ++acquired;
            if (frame.new_frame) {
                ++new_desktop_frames;
            }
        } else if (source->failed()) {
            // Prefer the source's own description (closed window, resize) over
            // the bare HRESULT.
            const std::string detail = source->last_message();
            if (!detail.empty()) {
                fail(detail);
            } else {
                char message[96];
                std::snprintf(message, sizeof(message), "capture failed: 0x%08lx",
                              static_cast<unsigned long>(source->last_error()));
                fail(message);
            }
            if (source->last_error() == DXGI_ERROR_DEVICE_REMOVED &&
                d3d.device()) {
                std::fprintf(stderr, "device removed reason: 0x%08lx\n",
                             static_cast<unsigned long>(
                                 d3d.device()->GetDeviceRemovedReason()));
            }
            break;
        } else if (source->access_lost()) {
            fail("capture access lost");
            break;
        }

        if (have_frame) {
            const int64_t pts_us =
                static_cast<int64_t>(encoded_frames) * 1'000'000 / config.fps;
            if (!encoder.Encode(converter.output_texture(), pts_us, on_packet)) {
                char message[96];
                std::snprintf(message, sizeof(message),
                              "encode failed at frame %d", encoded_frames);
                ++encode_errors;
                fail(message);
                break;
            }
            if (write_failed) {
                char message[96];
                std::snprintf(message, sizeof(message),
                              "muxer write failed at frame %d", encoded_frames);
                fail(message);
                break;
            }
            if (audio_enabled) {
                if (system_ok && system_capture.failed()) {
                    std::fprintf(stderr, "system audio capture failed: 0x%08lx\n",
                                 static_cast<unsigned long>(
                                     system_capture.last_error()));
                    system_capture.Stop();
                    audio_mixer.SetSystemSource(nullptr);
                    system_ok = false;
                }
                if (mic_ok && mic_capture.failed()) {
                    std::fprintf(stderr, "microphone capture failed: 0x%08lx\n",
                                 static_cast<unsigned long>(
                                     mic_capture.last_error()));
                    mic_capture.Stop();
                    audio_mixer.SetMicSource(nullptr);
                    mic_ok = false;
                }
                if (!system_ok && !mic_ok) {
                    ReportWarning(
                        "audio disabled for the remainder of the recording");
                    audio_enabled = false;
                }
            }
            if (audio_enabled) {
                const int64_t target_samples =
                    static_cast<int64_t>(encoded_frames + 1) *
                    kAudioSampleRate / config.fps;
                const int frames_to_mix =
                    static_cast<int>(target_samples - audio_samples_written);
                if (frames_to_mix > 0) {
                    audio_buffer.resize(
                        static_cast<size_t>(frames_to_mix) * kAudioChannels);
                    audio_mixer.Mix(audio_buffer.data(), frames_to_mix);
                    if (!aac.Encode(audio_buffer.data(), frames_to_mix,
                                    on_audio_packet)) {
                        char message[96];
                        std::snprintf(message, sizeof(message),
                                      "audio encode failed at frame %d",
                                      encoded_frames);
                        audio_encode_failed = true;
                        fail(message);
                        break;
                    }
                    if (write_failed) {
                        char message[96];
                        std::snprintf(message, sizeof(message),
                                      "audio write failed at frame %d",
                                      encoded_frames);
                        fail(message);
                        break;
                    }
                    audio_samples_written += frames_to_mix;
                }
            }
            ++encoded_frames;
            PublishStats(clock.NowUs());

            // Preview work runs after the stats publish so the hot encode/mux
            // path is unaffected. The readback is blocking but covers only the
            // small preview texture.
            if (have_frame && preview_enabled &&
                encoded_frames % preview_interval == 0) {
                PublishPreview(d3d.context(), last_source, last_source_rect);
            }
        }

        next_deadline_us += frame_interval_us;
        const int64_t after_us = clock.NowUs();
        if (after_us > next_deadline_us + frame_interval_us) {
            // A short stall should not cause two frames back-to-back.
            next_deadline_us = after_us + frame_interval_us;
        }
    }

    system_capture.Stop();
    mic_capture.Stop();

    if (config.duration_seconds > 0 &&
        static_cast<int64_t>(encoded_frames) * 100 < target_frames * 99) {
        std::fprintf(stderr,
                     "warning: achieved %d/%lld target frames; "
                     "output duration is shorter than requested\n",
                     encoded_frames, static_cast<long long>(target_frames));
    }

    if (write_failed) {
        fail("muxer write failed; skipping encoder flush");
    } else if (!encoder.Flush(on_packet)) {
        fail("encoder flush failed");
    } else if (write_failed) {
        fail("muxer write failed during encoder flush");
    } else if (audio_track && !audio_encode_failed) {
        // Flush even if both captures failed mid-recording: the AAC track was
        // already declared in the header and must not end abruptly.
        if (!aac.Flush(on_audio_packet)) {
            fail("AAC flush failed");
        } else if (write_failed) {
            fail("muxer write failed during AAC flush");
        }
    }
    if (!muxer.Close()) {
        // A stop before the first video packet leaves the muxer without a
        // header, so Close reports a failure even though nothing went wrong.
        if (!(stop_requested.load() && packets_written == 0 && !HasError())) {
            fail("muxer close failed");
        }
    }

    // Final publish after flush/close so elapsed_us includes the tail.
    PublishStats(clock.NowUs());
    MarkFinished();
}

Recorder::Recorder() : impl_(std::make_unique<Impl>()) {}

Recorder::~Recorder() { Stop(); }

bool Recorder::Start(const RecorderConfig& config, std::string& error) {
    // A previous run must be over before its promise and thread are reused.
    impl_->stop_requested.store(true);
    if (impl_->thread.joinable()) {
        impl_->thread.join();
    }

    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        impl_->error.clear();
        impl_->stats = RecorderStats{};
        impl_->latest_preview.reset();
    }
    impl_->preview_sequence = 0;
    impl_->preview_interval = 1;
    impl_->preview_enabled = false;
    impl_->preview_error_logged = false;
    impl_->preview_staging.Reset();
    impl_->preview_converter = VideoProcessor{};
    impl_->ResetRunState();
    impl_->stop_requested.store(false);
    impl_->running.store(false);
    impl_->finished.store(false);
    impl_->config = config;

    impl_->init_promise = std::promise<bool>();
    std::future<bool> ready = impl_->init_promise.get_future();
    impl_->thread = std::thread([this] {
        try {
            impl_->Run();
        } catch (const std::exception& exception) {
            impl_->RecordError(exception.what());
            impl_->PublishStats(QpcClock().NowUs());
            impl_->MarkFinished();
        } catch (...) {
            impl_->RecordError("recording thread failed");
            impl_->PublishStats(QpcClock().NowUs());
            impl_->MarkFinished();
        }
    });

    if (!ready.get()) {
        if (impl_->thread.joinable()) {
            impl_->thread.join();
        }
        std::lock_guard<std::mutex> lock(impl_->mutex);
        error = impl_->error;
        return false;
    }
    return true;
}

void Recorder::RequestStop() { impl_->stop_requested.store(true); }

void Recorder::Stop() {
    impl_->stop_requested.store(true);
    if (impl_->thread.joinable()) {
        impl_->thread.join();
    }
    impl_->running.store(false);
}

bool Recorder::Running() const { return impl_->running.load(); }

bool Recorder::Finished() const { return impl_->finished.load(); }

std::string Recorder::error() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->error;
}

RecorderStats Recorder::stats() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->stats;
}

std::shared_ptr<const PreviewFrame> Recorder::LatestPreview() const {
    // nullptr means no preview frame has been published yet (or previews are
    // disabled). The shared_ptr keeps the frame alive after the pull.
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->latest_preview;
}

}  // namespace hmmob
