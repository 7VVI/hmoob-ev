#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>

#include "core/capture/DxgiCapture.h"
#include "core/common/QpcClock.h"
#include "core/encoder/AmfEncoder.h"
#include "core/gfx/D3D11Device.h"
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

bool Parse(int argc, char** argv, Options& options) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto next = [&](const char* name) -> const char* {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "missing value for %s\n", name);
                return nullptr;
            }
            return argv[++i];
        };
        if (arg == "--output") {
            const char* v = next("--output"); if (!v) return false; options.output = v;
        } else if (arg == "--seconds") {
            const char* v = next("--seconds"); if (!v) return false; options.seconds = std::atoi(v);
        } else if (arg == "--fps") {
            const char* v = next("--fps"); if (!v) return false; options.fps = std::atoi(v);
        } else if (arg == "--codec") {
            const char* v = next("--codec"); if (!v) return false; options.codec = v;
        } else if (arg == "--display") {
            const char* v = next("--display"); if (!v) return false; options.display = std::atoi(v);
        } else if (arg == "--bitrate") {
            const char* v = next("--bitrate"); if (!v) return false; options.bitrate = std::atoi(v);
        } else if (arg == "--hdr") {
            options.hdr = true;
        } else {
            std::fprintf(stderr, "unknown option: %s\n", arg.c_str());
            return false;
        }
    }
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    Options options;
    if (!Parse(argc, argv, options)) {
        return 1;
    }
    if (options.codec != "hevc" && options.codec != "h264") {
        std::fprintf(stderr, "codec must be hevc or h264\n");
        return 1;
    }
    if (options.seconds <= 0 || options.fps <= 0 || options.bitrate <= 0) {
        std::fprintf(stderr, "seconds/fps/bitrate must be positive\n");
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
    if (options.display < 0 || options.display >= static_cast<int>(displays.size())) {
        std::fprintf(stderr, "display index out of range\n");
        return 1;
    }

    hmmob::DxgiCapture capture;
    if (!capture.Initialize(d3d, options.display, options.hdr)) {
        std::fprintf(stderr, "failed to initialize dxgi capture\n");
        return 1;
    }
    if (capture.hdr()) {
        std::fprintf(stderr, "HDR tone mapping is added in Task 10; run without --hdr\n");
        return 2;
    }

    const int width = capture.width();
    const int height = capture.height();
    std::printf("display=%d %dx%d format=%d fps=%d codec=%s bitrate=%d\n",
                options.display, width, height,
                static_cast<int>(capture.format()), options.fps,
                options.codec.c_str(), options.bitrate);

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
    int64_t frame_index = 0;
    int captured = 0;
    int encoded_frames = 0;
    int packets_written = 0;
    int drops = 0;
    int64_t encoded_bytes = 0;
    bool have_frame = false;
    bool failed = false;

    const auto on_packet = [&](const hmmob::AmfPacket& packet) {
        ++packets_written;
        encoded_bytes += static_cast<int64_t>(packet.data.size());
        if (!muxer.WritePacket(packet.data.data(), packet.data.size(),
                               packet.pts_us, packet.keyframe)) {
            failed = true;
        }
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
            if (frame.width != width || frame.height != height) {
                std::fprintf(stderr, "display mode changed: %dx%d -> %dx%d\n",
                             width, height, frame.width, frame.height);
                failed = true;
                break;
            }
            if (!converter.Convert(frame.texture.Get(), converter.output_texture())) {
                std::fprintf(stderr, "color conversion failed\n");
                failed = true;
                break;
            }
            have_frame = true;
            ++captured;
        } else if (capture.access_lost()) {
            std::fprintf(stderr, "capture access lost\n");
            failed = true;
            break;
        }

        if (have_frame) {
            const int64_t pts_us = frame_index * 1'000'000 / options.fps;
            if (!encoder.Encode(converter.output_texture(), pts_us, on_packet) || failed) {
                std::fprintf(stderr, "encode failed at frame %lld\n",
                             static_cast<long long>(frame_index));
                ++drops;
                failed = true;
                break;
            }
            ++encoded_frames;
            ++frame_index;
        }

        next_deadline_us += frame_interval_us;
        const int64_t after_us = clock.NowUs();
        if (after_us > next_deadline_us + frame_interval_us) {
            next_deadline_us = after_us;
        }
    }

    if (!encoder.Flush(on_packet)) {
        std::fprintf(stderr, "encoder flush failed\n");
        failed = true;
    }
    if (!muxer.Close()) {
        std::fprintf(stderr, "muxer close failed\n");
        failed = true;
    }

    const double elapsed_s =
        static_cast<double>(clock.NowUs() - start_us) / 1'000'000.0;
    std::printf("captured=%d encoded=%d packets=%d bytes=%lld drops=%d "
                "elapsed=%.2fs avg_fps=%.2f\n",
                captured, encoded_frames, packets_written,
                static_cast<long long>(encoded_bytes), drops, elapsed_s,
                encoded_frames / elapsed_s);
    return failed ? 2 : 0;
}
