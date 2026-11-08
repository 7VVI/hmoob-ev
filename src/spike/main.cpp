#include <windows.h>

#include <dwmapi.h>

#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

#include "core/capture/CameraSource.h"
#include "core/recorder/Recorder.h"

namespace {

struct Options {
    std::string output = "out.mp4";
    int seconds = 10;
    int fps = 60;
    std::string codec = "hevc";
    int display = 0;
    bool has_region = false;
    int region_x = 0;
    int region_y = 0;
    int region_width = 0;
    int region_height = 0;
    int window = 0;  // 1-based index into the enumerated window list
    bool has_camera = false;
    int camera = 0;
    bool list_cameras = false;
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
                 "  --region <x>,<y>,<w>,<h>  record one region of the display\n"
                 "  --window <n>            record the n-th listed visible top-level window\n"
                 "  --camera <n>            record the n-th camera device (default: 0)\n"
                 "  --list-cameras          list camera devices and exit\n"
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

bool ParseRegion(const char* text, int (&values)[4]) {
    if (text == nullptr || *text == '\0') {
        return false;
    }
    const std::string input = text;
    size_t start = 0;
    for (int i = 0; i < 4; ++i) {
        const size_t comma = input.find(',', start);
        const bool last = i == 3;
        if (last ? comma != std::string::npos : comma == std::string::npos) {
            return false;
        }
        const size_t end = last ? input.size() : comma;
        if (end == start) {
            return false;
        }
        const std::string part = input.substr(start, end - start);
        if (!ParseInt(part.c_str(), values[i])) {
            return false;
        }
        start = end + 1;
    }
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
        } else if (arg == "--region") {
            const char* v = next("--region");
            int values[4] = {};
            if (!v || !ParseRegion(v, values)) {
                std::fprintf(stderr,
                             "invalid value for --region (expected x,y,w,h)\n");
                return ParseResult::Error;
            }
            options.has_region = true;
            options.region_x = values[0];
            options.region_y = values[1];
            options.region_width = values[2];
            options.region_height = values[3];
        } else if (arg == "--window") {
            const char* v = next("--window");
            if (!v || !ParseInt(v, options.window)) {
                std::fprintf(stderr, "invalid value for --window\n");
                return ParseResult::Error;
            }
        } else if (arg == "--camera") {
            const char* v = next("--camera");
            if (!v || !ParseInt(v, options.camera)) {
                std::fprintf(stderr, "invalid value for --camera\n");
                return ParseResult::Error;
            }
            options.has_camera = true;
        } else if (arg == "--list-cameras") {
            options.list_cameras = true;
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

std::string Narrow(const std::wstring& text) {
    if (text.empty()) {
        return {};
    }
    const int length = WideCharToMultiByte(
        CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0,
        nullptr, nullptr);
    if (length <= 0) {
        return {};
    }
    std::string result(static_cast<size_t>(length), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                        result.data(), length, nullptr, nullptr);
    return result;
}

bool IsCloaked(HWND window) {
    BOOL cloaked = FALSE;
    const HRESULT hr =
        DwmGetWindowAttribute(window, DWMWA_CLOAKED, &cloaked, sizeof(cloaked));
    return SUCCEEDED(hr) && cloaked != FALSE;
}

struct WindowEntry {
    HWND window = nullptr;
    std::wstring title;
};

BOOL CALLBACK CollectWindow(HWND window, LPARAM param) {
    auto& windows = *reinterpret_cast<std::vector<WindowEntry>*>(param);
    if (!IsWindowVisible(window) || IsCloaked(window)) {
        return TRUE;
    }
    if ((GetWindowLongPtrW(window, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) != 0) {
        return TRUE;
    }
    RECT bounds{};
    if (!GetWindowRect(window, &bounds) || bounds.right <= bounds.left ||
        bounds.bottom <= bounds.top) {
        return TRUE;
    }
    const int length = GetWindowTextLengthW(window);
    if (length <= 0) {
        return TRUE;
    }
    std::wstring title(static_cast<size_t>(length) + 1, L'\0');
    const int copied = GetWindowTextW(window, title.data(), length + 1);
    if (copied <= 0) {
        return TRUE;
    }
    title.resize(static_cast<size_t>(copied));
    if (title.find_first_not_of(L" \t\r\n") == std::wstring::npos) {
        return TRUE;
    }
    windows.push_back({window, std::move(title)});
    return TRUE;
}

// Lists the visible top-level windows and returns the 1-based `index`-th one.
HWND SelectWindow(int index, std::string& error) {
    std::vector<WindowEntry> windows;
    EnumWindows(&CollectWindow, reinterpret_cast<LPARAM>(&windows));
    if (windows.empty()) {
        error = "no visible window found";
        return nullptr;
    }
    std::printf("windows:\n");
    for (size_t i = 0; i < windows.size(); ++i) {
        std::printf("  [%zu] %s\n", i + 1, Narrow(windows[i].title).c_str());
    }
    if (index < 1 || index > static_cast<int>(windows.size())) {
        char message[128];
        std::snprintf(message, sizeof(message),
                      "window index %d is out of range (1..%zu)", index,
                      windows.size());
        error = message;
        return nullptr;
    }
    const WindowEntry& chosen = windows[static_cast<size_t>(index - 1)];
    std::printf("selected window [%d] %s\n", index,
                Narrow(chosen.title).c_str());
    return chosen.window;
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
    if (options.list_cameras) {
        const std::vector<hmmob::CameraSource::CameraDeviceInfo> devices =
            hmmob::CameraSource::EnumerateDevices();
        if (devices.empty()) {
            std::printf("no camera found\n");
            return 0;
        }
        std::printf("cameras:\n");
        for (size_t i = 0; i < devices.size(); ++i) {
            std::printf("  [%zu] %s\n", i, devices[i].name.c_str());
        }
        return 0;
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
    if (options.has_region &&
        (options.region_x < 0 || options.region_y < 0 ||
         options.region_width <= 0 || options.region_height <= 0)) {
        std::fprintf(stderr,
                     "--region needs non-negative x,y and positive w,h\n");
        return 1;
    }
    if (options.window < 0) {
        std::fprintf(stderr, "--window must be non-negative\n");
        return 1;
    }
    if (options.has_camera && options.camera < 0) {
        std::fprintf(stderr, "--camera must be non-negative\n");
        return 1;
    }
    if (options.window > 0 && options.has_region) {
        std::fprintf(stderr, "--window cannot be combined with --region\n");
        return 1;
    }
    if (options.has_camera && (options.window > 0 || options.has_region)) {
        std::fprintf(stderr,
                     "--camera cannot be combined with --window or --region\n");
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
    if (options.has_camera) {
        config.mode = hmmob::CaptureMode::Camera;
        config.camera_index = options.camera;
    } else if (options.window > 0) {
        std::string select_error;
        const HWND window = SelectWindow(options.window, select_error);
        if (!window) {
            std::fprintf(stderr, "%s\n", select_error.c_str());
            return 1;
        }
        config.mode = hmmob::CaptureMode::Window;
        config.window = window;
    } else if (options.has_region) {
        config.mode = hmmob::CaptureMode::Region;
        config.region_x = options.region_x;
        config.region_y = options.region_y;
        config.region_width = options.region_width;
        config.region_height = options.region_height;
    }
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
