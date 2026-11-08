#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <system_error>
#include <thread>

#include "core/recorder/Recorder.h"

namespace {

struct Options {
    std::string output = "out.mp4";
    int seconds = 10;
    int fps = 60;
    std::string codec = "hevc";
    int display = 0;
    int bitrate = 50000000;
    bool hdr = false;
    bool audio = true;
    bool system_audio = true;
    bool microphone = true;
    float system_volume = 1.0f;
    float mic_volume = 1.0f;
};

enum class ParseResult { Ok, Help, Error };

void PrintUsage(std::FILE* stream) {
    std::fprintf(stream,
                 "usage: evspike [options]\n"
                 "  --output <path>         output MP4 path (default: out.mp4)\n"
                 "  --seconds <n>           recording length in seconds, > 0 (default: 10)\n"
                 "  --fps <n>               target frame rate, 1..1000 (default: 60)\n"
                 "  --codec <name>          hevc or h264 (default: hevc)\n"
                 "  --display <n>           display index, >= 0 (default: 0)\n"
                 "  --bitrate <bps>         target bitrate in bits/s, > 0 (default: 50000000)\n"
                 "  --hdr                   request HDR capture (tone mapped to SDR)\n"
                 "  --no-audio              disable all audio\n"
                 "  --no-system-audio       disable system loopback audio\n"
                 "  --no-mic                disable microphone\n"
                 "  --system-volume <x>     system audio gain, 0..4 (default: 1.0)\n"
                 "  --mic-volume <x>        microphone gain, 0..4 (default: 1.0)\n"
                 "  -h, --help              show this help and exit\n");
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

bool ParseFloat(const char* text, float& out) {
    if (text == nullptr || *text == '\0') {
        return false;
    }
    char* end = nullptr;
    const float value = std::strtof(text, &end);
    if (end == text || *end != '\0' || !std::isfinite(value)) {
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
        } else if (arg == "--no-audio") {
            options.audio = false;
        } else if (arg == "--no-system-audio") {
            options.system_audio = false;
        } else if (arg == "--no-mic") {
            options.microphone = false;
        } else if (arg == "--system-volume") {
            const char* v = next("--system-volume");
            if (!v || !ParseFloat(v, options.system_volume)) {
                std::fprintf(stderr, "invalid value for --system-volume\n");
                return ParseResult::Error;
            }
        } else if (arg == "--mic-volume") {
            const char* v = next("--mic-volume");
            if (!v || !ParseFloat(v, options.mic_volume)) {
                std::fprintf(stderr, "invalid value for --mic-volume\n");
                return ParseResult::Error;
            }
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
    if (options.system_volume < 0.0f || options.system_volume > 4.0f) {
        std::fprintf(stderr, "--system-volume must be between 0 and 4\n");
        PrintUsage(stderr);
        return 1;
    }
    if (options.mic_volume < 0.0f || options.mic_volume > 4.0f) {
        std::fprintf(stderr, "--mic-volume must be between 0 and 4\n");
        PrintUsage(stderr);
        return 1;
    }

    hmmob::RecorderConfig config;
    config.display_index = options.display;
    config.fps = options.fps;
    config.codec = options.codec;
    config.bitrate = options.bitrate;
    config.hdr = options.hdr;
    config.system_audio = options.audio && options.system_audio;
    config.microphone = options.audio && options.microphone;
    config.system_gain = options.system_volume;
    config.mic_gain = options.mic_volume;
    config.output_path = options.output;
    config.duration_seconds = options.seconds;

    hmmob::Recorder recorder;
    std::string error;
    if (!recorder.Start(config, error)) {
        std::fprintf(stderr, "%s\n", error.c_str());
        return 1;
    }

    hmmob::RecorderStats stats = recorder.stats();
    std::printf("display=%d %dx%d format=%d fps=%d codec=%s bitrate=%d\n",
                options.display, stats.display_width, stats.display_height,
                stats.display_format, options.fps, options.codec.c_str(),
                options.bitrate);

    while (recorder.Running()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    stats = recorder.stats();
    const double elapsed_s =
        static_cast<double>(stats.elapsed_us) / 1'000'000.0;
    const double avg_fps =
        elapsed_s > 0.0 ? static_cast<double>(stats.encoded_frames) / elapsed_s
                        : 0.0;
    std::printf("acquired=%d new_desktop=%d encoded=%d packets=%d bytes=%lld "
                "encode_errors=%d audio_packets=%d audio_bytes=%lld "
                "elapsed=%.2fs avg_fps=%.2f\n",
                stats.acquired, stats.new_desktop_frames, stats.encoded_frames,
                stats.packets_written,
                static_cast<long long>(stats.encoded_bytes),
                stats.encode_errors, stats.audio_packets,
                static_cast<long long>(stats.audio_bytes), elapsed_s, avg_fps);

    const std::string final_error = recorder.error();
    if (!final_error.empty()) {
        std::fprintf(stderr, "%s\n", final_error.c_str());
        return 1;
    }
    return 0;
}
