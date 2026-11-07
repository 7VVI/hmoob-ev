#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstdio>
#include <string>
#include <system_error>
#include <thread>

#include "core/capture/DxgiCapture.h"
#include "core/common/QpcClock.h"
#include "core/encoder/AmfEncoder.h"
#include "core/gfx/D3D11Device.h"
#include "core/gfx/HdrToneMapper.h"
#include "core/gfx/VideoProcessor.h"
#include "core/mux/FfmpegMuxer.h"

namespace {

struct Options {
    std::string output = "out.mp4";
    int seconds = 10;
    int fps = 60;
    std::string codec = "hevc";
    int display = 0;
    int bitrate = 50000000;
    bool hdr = false;
};

enum class ParseResult { Ok, Help, Error };

void PrintUsage(std::FILE* stream) {
    std::fprintf(stream,
                 "usage: evspike [options]\n"
                 "  --output <path>   output MP4 path (default: out.mp4)\n"
                 "  --seconds <n>     recording length in seconds, > 0 (default: 10)\n"
                 "  --fps <n>         target frame rate, 1..1000 (default: 60)\n"
                 "  --codec <name>    hevc or h264 (default: hevc)\n"
                 "  --display <n>     display index, >= 0 (default: 0)\n"
                 "  --bitrate <bps>   target bitrate in bits/s, > 0 (default: 50000000)\n"
                 "  --hdr             request HDR capture (tone mapped to SDR)\n"
                 "  -h, --help        show this help and exit\n");
}

bool ParseInt(const char* text, int& out) {
    if (text == nullptr || *text == '\0') {
        return false;
    }
    const char* end = text;
    while (*end != '\0') {
        ++end;
    }
    int value = 0;
    const auto result = std::from_chars(text, end, value);
    if (result.ec != std::errc() || result.ptr != end) {
        return false;
    }
    out = value;
    return true;
}

ParseResult Parse(int argc, char** argv, Options& options) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto next = [&](const char* name) -> const char* {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "missing value for %s\n", name);
                return nullptr;
            }
            return argv[++i];
        };
        if (arg == "--help" || arg == "-h") {
            return ParseResult::Help;
        } else if (arg == "--output") {
            const char* v = next("--output"); if (!v) return ParseResult::Error; options.output = v;
        } else if (arg == "--seconds") {
            const char* v = next("--seconds");
            if (!v || !ParseInt(v, options.seconds)) {
                std::fprintf(stderr, "invalid value for --seconds\n");
                return ParseResult::Error;
            }
        } else if (arg == "--fps") {
            const char* v = next("--fps");
            if (!v || !ParseInt(v, options.fps)) {
                std::fprintf(stderr, "invalid value for --fps\n");
                return ParseResult::Error;
            }
        } else if (arg == "--codec") {
            const char* v = next("--codec"); if (!v) return ParseResult::Error; options.codec = v;
        } else if (arg == "--display") {
            const char* v = next("--display");
            if (!v || !ParseInt(v, options.display)) {
                std::fprintf(stderr, "invalid value for --display\n");
                return ParseResult::Error;
            }
        } else if (arg == "--bitrate") {
            const char* v = next("--bitrate");
            if (!v || !ParseInt(v, options.bitrate)) {
                std::fprintf(stderr, "invalid value for --bitrate\n");
                return ParseResult::Error;
            }
        } else if (arg == "--hdr") {
            options.hdr = true;
        } else {
            std::fprintf(stderr, "unknown option: %s\n", arg.c_str());
            return ParseResult::Error;
        }
    }
    return ParseResult::Ok;
}

}  // namespace

int main(int argc, char** argv) {
    Options options;
    const ParseResult parsed = Parse(argc, argv, options);
    if (parsed == ParseResult::Help) {
        PrintUsage(stdout);
        return 0;
    }
    if (parsed == ParseResult::Error) {
        PrintUsage(stderr);
        return 1;
    }
    if (options.codec != "hevc" && options.codec != "h264") {
        std::fprintf(stderr, "codec must be hevc or h264\n");
        return 1;
    }
    if (options.seconds <= 0) {
        std::fprintf(stderr, "--seconds must be positive\n");
        return 1;
    }
    if (options.fps < 1 || options.fps > 1000) {
        std::fprintf(stderr, "--fps must be between 1 and 1000\n");
        return 1;
    }
    if (options.bitrate <= 0) {
        std::fprintf(stderr, "--bitrate must be positive\n");
        return 1;
    }
    if (options.display < 0) {
        std::fprintf(stderr, "--display must be non-negative\n");
        return 1;
    }

    hmmob::D3D11Device d3d;
    if (!d3d.Create()) {
        std::fprintf(stderr, "failed to create d3d11 device\n");
        return 1;
    }

    const auto displays = d3d.EnumerateDisplays();
    if (displays.empty()) {
        std::fprintf(stderr, "no display found\n");
        return 1;
    }
    if (options.display >= static_cast<int>(displays.size())) {
        std::fprintf(stderr, "display index out of range\n");
        return 1;
    }

    hmmob::DxgiCapture capture;
    if (!capture.Initialize(d3d, options.display, options.hdr)) {
        std::fprintf(stderr, "failed to initialize dxgi capture\n");
        return 1;
    }
    if (capture.hdr_downgraded()) {
        std::fprintf(stderr,
                     "warning: HDR capture was requested but the display is SDR; "
                     "recording SDR\n");
    }

    const int width = capture.width();
    const int height = capture.height();
    const DXGI_FORMAT format = capture.format();
    std::printf("display=%d %dx%d format=%d fps=%d codec=%s bitrate=%d\n",
                options.display, width, height, static_cast<int>(format),
                options.fps, options.codec.c_str(), options.bitrate);

    // PQ vs scRGB cannot be inferred from the surface format alone: HDR10
    // streams PQ while scRGB (RGB_FULL_G10_NONE_P709) is already linear.
    const bool hdr_display = hmmob::IsHdrColorSpace(capture.color_space());
    const bool pq_mode =
        capture.color_space() == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020 ||
        capture.color_space() == DXGI_COLOR_SPACE_RGB_STUDIO_G2084_NONE_P2020;
    // Don't compile shaders or allocate a full-resolution render target when
    // capture is SDR.
    const bool tone_map_hdr = capture.hdr() && hdr_display;
    hmmob::HdrToneMapper tone_mapper;
    if (tone_map_hdr && !tone_mapper.Initialize(d3d, width, height, pq_mode)) {
        std::fprintf(stderr, "failed to initialize HDR tone mapper\n");
        return 1;
    }

    hmmob::VideoProcessor converter;
    if (!converter.Initialize(d3d, width, height)) {
        std::fprintf(stderr, "failed to initialize video processor\n");
        return 1;
    }

    hmmob::AmfEncoder encoder;
    if (!encoder.Initialize(d3d.device(), options.codec, width, height,
                            options.fps, options.bitrate)) {
        std::fprintf(stderr, "failed to initialize AMF encoder\n");
        return 1;
    }

    hmmob::FfmpegMuxer muxer;
    if (!muxer.Open(options.output, options.codec, width, height,
                    options.fps, options.bitrate)) {
        std::fprintf(stderr, "failed to open output %s\n", options.output.c_str());
        return 1;
    }

    hmmob::QpcClock clock;
    const int64_t start_us = clock.NowUs();
    const int64_t frame_interval_us = 1'000'000 / options.fps;
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

    const auto on_packet = [&](const hmmob::AmfPacket& packet) {
        if (!muxer.WritePacket(packet.data.data(), packet.data.size(),
                               packet.pts_us, packet.keyframe)) {
            write_failed = true;
            return;
        }
        ++packets_written;
        encoded_bytes += static_cast<int64_t>(packet.data.size());
    };

    while (clock.NowUs() - start_us <
           static_cast<int64_t>(options.seconds) * 1'000'000) {
        const int64_t now_us = clock.NowUs();
        if (now_us < next_deadline_us) {
            const int64_t sleep_us =
                std::min<int64_t>(next_deadline_us - now_us, 1000);
            std::this_thread::sleep_for(std::chrono::microseconds(sleep_us));
            continue;
        }
        if (failed) {
            break;
        }

        hmmob::CaptureFrame frame;
        if (capture.Acquire(frame, 0)) {
            if (frame.width != width || frame.height != height ||
                frame.format != format) {
                std::fprintf(stderr,
                             "display mode changed: %dx%d fmt %d -> %dx%d fmt %d\n",
                             width, height, static_cast<int>(format),
                             frame.width, frame.height,
                             static_cast<int>(frame.format));
                failed = true;
                break;
            }
            ID3D11Texture2D* source = frame.texture.Get();
            if (tone_map_hdr) {
                source = tone_mapper.Map(frame.texture.Get());
                if (!source) {
                    std::fprintf(stderr, "HDR tone mapping failed\n");
                    failed = true;
                    break;
                }
            } else if (frame.hdr) {
                std::fprintf(stderr,
                             "HDR capture format %d with SDR color space %d is "
                             "not supported in Phase 0\n",
                             static_cast<int>(frame.format),
                             static_cast<int>(capture.color_space()));
                failed = true;
                break;
            }
            if (!converter.Convert(source, converter.output_texture())) {
                std::fprintf(stderr, "color conversion failed\n");
                failed = true;
                break;
            }
            have_frame = true;
            ++acquired;
            if (frame.new_desktop) {
                ++new_desktop_frames;
            }
        } else if (capture.failed()) {
            std::fprintf(stderr, "capture failed: 0x%08lx\n",
                         static_cast<unsigned long>(capture.last_error()));
            if (capture.last_error() == DXGI_ERROR_DEVICE_REMOVED && d3d.device()) {
                std::fprintf(stderr, "device removed reason: 0x%08lx\n",
                             static_cast<unsigned long>(
                                 d3d.device()->GetDeviceRemovedReason()));
            }
            failed = true;
            break;
        } else if (capture.access_lost()) {
            std::fprintf(stderr, "capture access lost\n");
            failed = true;
            break;
        }

        if (have_frame) {
            const int64_t pts_us =
                static_cast<int64_t>(encoded_frames) * 1'000'000 / options.fps;
            if (!encoder.Encode(converter.output_texture(), pts_us, on_packet)) {
                std::fprintf(stderr, "encode failed at frame %d\n", encoded_frames);
                ++encode_errors;
                failed = true;
                break;
            }
            if (write_failed) {
                std::fprintf(stderr, "muxer write failed at frame %d\n",
                             encoded_frames);
                failed = true;
                break;
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

    const int64_t target_frames =
        static_cast<int64_t>(options.seconds) * options.fps;
    if (static_cast<int64_t>(encoded_frames) * 100 < target_frames * 99) {
        std::fprintf(stderr,
                     "warning: achieved %d/%lld target frames; "
                     "output duration is shorter than requested\n",
                     encoded_frames, static_cast<long long>(target_frames));
    }

    if (write_failed) {
        std::fprintf(stderr, "muxer write failed; skipping encoder flush\n");
        failed = true;
    } else if (!encoder.Flush(on_packet)) {
        std::fprintf(stderr, "encoder flush failed\n");
        failed = true;
    } else if (write_failed) {
        std::fprintf(stderr, "muxer write failed during encoder flush\n");
        failed = true;
    }
    if (!muxer.Close()) {
        std::fprintf(stderr, "muxer close failed\n");
        failed = true;
    }

    const double elapsed_s =
        static_cast<double>(clock.NowUs() - start_us) / 1'000'000.0;
    std::printf("acquired=%d new_desktop=%d encoded=%d packets=%d bytes=%lld "
                "encode_errors=%d elapsed=%.2fs avg_fps=%.2f\n",
                acquired, new_desktop_frames, encoded_frames, packets_written,
                static_cast<long long>(encoded_bytes), encode_errors,
                elapsed_s, encoded_frames / elapsed_s);
    return failed ? 1 : 0;
}
