#include "core/recorder/Recorder.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <exception>
#include <future>
#include <objbase.h>
#include <utility>

#include "core/audio/AacEncoder.h"
#include "core/audio/AudioMixer.h"
#include "core/audio/WasapiCapture.h"
#include "core/capture/DxgiCapture.h"
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

    void Run();

    // Keeps the first failure: later cleanup failures are consequences of it.
    void RecordError(std::string message) {
        std::lock_guard<std::mutex> lock(mutex);
        if (error.empty()) {
            error = std::move(message);
        }
    }

    void MarkFinished() {
        running.store(false);
        finished.store(true);
    }
};

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

    DxgiCapture capture;
    if (!capture.Initialize(d3d, config.display_index, config.hdr)) {
        fail_init("failed to initialize dxgi capture");
        return;
    }
    if (capture.hdr_downgraded()) {
        ReportWarning(
            "warning: HDR capture was requested but the display is SDR; "
            "recording SDR");
    }

    const int width = capture.width();
    const int height = capture.height();
    const DXGI_FORMAT format = capture.format();
    {
        std::lock_guard<std::mutex> lock(mutex);
        stats.display_width = width;
        stats.display_height = height;
        stats.display_format = static_cast<int>(format);
    }

    // PQ vs scRGB cannot be inferred from the surface format alone: HDR10
    // streams PQ while scRGB (RGB_FULL_G10_NONE_P709) is already linear.
    const bool hdr_display = IsHdrColorSpace(capture.color_space());
    const bool pq_mode =
        capture.color_space() == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020 ||
        capture.color_space() == DXGI_COLOR_SPACE_RGB_STUDIO_G2084_NONE_P2020;
    // Don't compile shaders or allocate a full-resolution render target when
    // capture is SDR.
    const bool tone_map_hdr = capture.hdr() && hdr_display;
    HdrToneMapper tone_mapper;
    if (tone_map_hdr && !tone_mapper.Initialize(d3d, width, height, pq_mode)) {
        fail_init("failed to initialize HDR tone mapper");
        return;
    }

    VideoProcessor converter;
    if (!converter.Initialize(d3d, width, height)) {
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
    const bool audio_track = audio_enabled;

    QpcClock clock;
    const int64_t start_us = clock.NowUs();
    const int64_t frame_interval_us = 1'000'000 / config.fps;
    int64_t next_deadline_us = start_us;
    int acquired = 0;
    int new_desktop_frames = 0;
    int encoded_frames = 0;
    int packets_written = 0;
    int encode_errors = 0;
    int64_t encoded_bytes = 0;
    bool have_frame = false;
    bool failed = false;
    bool write_failed = false;

    const auto fail = [&](std::string message) {
        RecordError(std::move(message));
        failed = true;
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

    int audio_packets = 0;
    int64_t audio_bytes = 0;
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
        if (failed) {
            break;
        }

        CaptureFrame frame;
        if (capture.Acquire(frame, 0)) {
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
            ID3D11Texture2D* source = frame.texture.Get();
            if (tone_map_hdr) {
                source = tone_mapper.Map(frame.texture.Get());
                if (!source) {
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
                    static_cast<int>(capture.color_space()));
                fail(message);
                break;
            }
            if (!converter.Convert(source, converter.output_texture())) {
                fail("color conversion failed");
                break;
            }
            have_frame = true;
            ++acquired;
            if (frame.new_desktop) {
                ++new_desktop_frames;
            }
        } else if (capture.failed()) {
            char message[96];
            std::snprintf(message, sizeof(message), "capture failed: 0x%08lx",
                          static_cast<unsigned long>(capture.last_error()));
            fail(message);
            if (capture.last_error() == DXGI_ERROR_DEVICE_REMOVED &&
                d3d.device()) {
                std::fprintf(stderr, "device removed reason: 0x%08lx\n",
                             static_cast<unsigned long>(
                                 d3d.device()->GetDeviceRemovedReason()));
            }
            break;
        } else if (capture.access_lost()) {
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

    const int64_t target_frames =
        static_cast<int64_t>(config.duration_seconds) * config.fps;
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
        fail("muxer close failed");
    }

    {
        std::lock_guard<std::mutex> lock(mutex);
        stats.elapsed_us = clock.NowUs() - start_us;
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
    }
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
            impl_->MarkFinished();
        } catch (...) {
            impl_->RecordError("recording thread failed");
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
    // Frames are added in the preview task; nullptr means "nothing yet".
    return nullptr;
}

}  // namespace hmmob
