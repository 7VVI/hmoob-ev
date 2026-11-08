# Hmmob EV 最小 Qt 界面（含实时预览）实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 提供可双击启动的 Qt 窗口程序，用户可选择显示器/帧率/编码/码率、开关系统声与麦克风、选择输出路径，开始/停止录制，并看到实时预览与统计，产出可播放的 MP4。

**Architecture:** 把现有 `evspike/main.cpp` 的录制流程抽成 `hmmob::Recorder`（独立工作线程，拥有自己的 D3D11 设备、采集、转换、编码、封装与音频）。`evspike` 与新的 Qt 程序共用该引擎。预览由 Recorder 在工作线程内用第二个 `VideoProcessor` 缩放到 640×360 BGRA，读回 CPU 后以 `shared_ptr<const PreviewFrame>` 发布；UI 以 30 Hz 拉取最新帧交给 `PreviewWidget` 绘制。

**Tech Stack:** C++20、Qt 6 Widgets（vcpkg `qtbase[gui,widgets]`）、D3D11、AMD AMF、FFmpeg 9、Catch2 3。

---

## 文件结构

| 文件 | 职责 |
| --- | --- |
| `src/core/gfx/VideoProcessor.h/.cpp` | 增加「任意输出尺寸 + 输出格式」初始化，保留原 NV12 接口 |
| `src/core/recorder/Recorder.h/.cpp` | 录制引擎：线程、管线、统计、停止、预览帧发布 |
| `src/spike/main.cpp` | 改为参数解析 + 调用 Recorder + 打印统计 |
| `src/app/main.cpp` | QApplication 入口 |
| `src/app/MainWindow.h/.cpp` | 控件、计时器、开始/停止、错误显示、关闭确认 |
| `src/app/PreviewWidget.h/.cpp` | 绘制最新预览帧 |
| `tests/test_video_processor.cpp` | 新增缩放 BGRA 输出测试 |
| `CMakeLists.txt` | Qt 目标、windeployqt 部署 |
| `README.md` | 界面运行说明 |

---

### Task 1: VideoProcessor 支持缩放与非 NV12 输出

**Files:**
- Modify: `src/core/gfx/VideoProcessor.h`
- Modify: `src/core/gfx/VideoProcessor.cpp`
- Test: `tests/test_video_processor.cpp`

- [ ] **Step 1: 写失败测试**

在 `tests/test_video_processor.cpp` 末尾追加（文件已有 `CreateBgra` 等辅助函数时复用；没有则按现有测试的写法创建输入纹理）：

```cpp
TEST_CASE("VideoProcessor scales BGRA to a smaller BGRA output") {
    hmmob::D3D11Device d3d;
    if (!d3d.Create()) {
        SKIP("D3D11 device unavailable");
    }

    hmmob::VideoProcessor converter;
    REQUIRE(converter.Initialize(d3d, 64, 64, 32, 32, DXGI_FORMAT_B8G8R8A8_UNORM));
    REQUIRE(converter.output_width() == 32);
    REQUIRE(converter.output_height() == 32);

    auto input = CreateBgra(d3d, 64, 64, 0xFF0000FF /* BGRA red */);
    REQUIRE(input);
    REQUIRE(converter.Convert(input.Get(), converter.output_texture()));

    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = 32;
    desc.Height = 32;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_STAGING;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> staging;
    REQUIRE(SUCCEEDED(d3d.device()->CreateTexture2D(&desc, nullptr, &staging)));
    d3d.context()->CopyResource(staging.Get(), converter.output_texture());

    D3D11_MAPPED_SUBRESOURCE mapped{};
    REQUIRE(SUCCEEDED(d3d.context()->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)));
    const auto* row = static_cast<const uint8_t*>(mapped.pData) + 16 * mapped.RowPitch + 16 * 4;
    CHECK(row[0] < 16);    // blue
    CHECK(row[1] < 16);    // green
    CHECK(row[2] > 240);   // red
    d3d.context()->Unmap(staging.Get(), 0);
}
```

若 `CreateBgra` 在匿名命名空间内不可见也没关系——测试文件内资源同文件，直接调用即可。

- [ ] **Step 2: 运行测试确认失败**

```powershell
$cmake = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
& $cmake --build build/release --config Release
& $cmake -E env ... # 不适用；改用 ctest -R "VideoProcessor"
```

预期：编译失败（`Initialize` 五参数重载不存在、`output_width()` 不存在）。

- [ ] **Step 3: 头文件改造**

替换 `src/core/gfx/VideoProcessor.h` 中的公开接口：

```cpp
class VideoProcessor {
public:
    // 原接口：等尺寸 BGRA -> NV12。
    [[nodiscard]] bool Initialize(D3D11Device& d3d, int width, int height);
    // 通用接口：任意输入/输出尺寸与输出格式（NV12 或 BGRA8）。
    [[nodiscard]] bool Initialize(D3D11Device& d3d, int input_width,
                                  int input_height, int output_width,
                                  int output_height, DXGI_FORMAT output_format);
    // `output` 必须是 output_texture()。
    [[nodiscard]] bool Convert(ID3D11Texture2D* input, ID3D11Texture2D* output);
    ID3D11Texture2D* output_texture() const { return output_.Get(); }
    int width() const { return input_width_; }
    int height() const { return input_height_; }
    int output_width() const { return output_width_; }
    int output_height() const { return output_height_; }
    DXGI_FORMAT output_format() const { return output_format_; }

private:
    void Reset();
    // ...原有成员改为：
    ID3D11Device* device_ = nullptr;
    ID3D11DeviceContext* context_ = nullptr;
    Microsoft::WRL::ComPtr<ID3D11VideoDevice> video_device_;
    Microsoft::WRL::ComPtr<ID3D11VideoContext> video_context_;
    Microsoft::WRL::ComPtr<ID3D11VideoProcessorEnumerator> enumerator_;
    Microsoft::WRL::ComPtr<ID3D11VideoProcessor> processor_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> output_;
    Microsoft::WRL::ComPtr<ID3D11VideoProcessorInputView> input_view_;
    Microsoft::WRL::ComPtr<ID3D11VideoProcessorOutputView> output_view_;
    ID3D11Texture2D* cached_input_ = nullptr;
    int input_width_ = 0;
    int input_height_ = 0;
    int output_width_ = 0;
    int output_height_ = 0;
    DXGI_FORMAT output_format_ = DXGI_FORMAT_NV12;
    bool ready_ = false;
};
```

- [ ] **Step 4: 实现**

`Reset()` 清零新增字段（`output_format_ = DXGI_FORMAT_NV12`）。

`Initialize(d3d, width, height)` 直接委托：

```cpp
bool VideoProcessor::Initialize(D3D11Device& d3d, int width, int height) {
    return Initialize(d3d, width, height, width, height, DXGI_FORMAT_NV12);
}
```

五参数版本：

- 校验 `output_format` 是 `DXGI_FORMAT_NV12` 或 `DXGI_FORMAT_B8G8R8A8_UNORM`，输入/输出尺寸均 > 0。
- `content.InputWidth/Height` 用输入尺寸，`content.OutputWidth/Height` 用输出尺寸，帧率保持 60/1。
- 输出纹理 `Width/Height` 用输出尺寸、`Format = output_format_`。
- 输出颜色空间：NV12 保持现有 `Nominal_Range=16_235`；BGRA 输出把 `output_cs.Nominal_Range` 设为 `D3D11_VIDEO_PROCESSOR_NOMINAL_RANGE_0_255`。
- `Convert` 的输入校验改为 `src_desc.Width == input_width_ && src_desc.Height == input_height_` 且格式为 BGRA8；输出校验改为 `dst_desc.Format == output_format_ && 宽高 == output_*`。
- 其余（缓存 input view、Blt）不变。

- [ ] **Step 5: 运行全部测试**

```powershell
& $cmake --build build/release --config Release
& "C:\...\CMake\bin\ctest.exe" --test-dir build/release -C Release --output-on-failure
```

预期：82/82 通过（新增 1 个），原有 NV12 测试不回退。

- [ ] **Step 6: 提交**

```powershell
git add src/core/gfx/VideoProcessor.h src/core/gfx/VideoProcessor.cpp tests/test_video_processor.cpp
git commit -m "feat: support scaled bgra output in video processor"
```

---

### Task 2: 抽出 Recorder 录制引擎

**Files:**
- Create: `src/core/recorder/Recorder.h`
- Create: `src/core/recorder/Recorder.cpp`
- Modify: `src/spike/main.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: 定义接口**

`src/core/recorder/Recorder.h`：

```cpp
#pragma once
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>

namespace hmmob {

struct RecorderConfig {
    int display_index = 0;
    int fps = 60;
    std::string codec = "hevc";  // "hevc" or "h264"
    int64_t bitrate = 50000000;
    bool hdr = false;
    bool system_audio = true;
    bool microphone = true;
    float system_gain = 1.0f;
    float mic_gain = 1.0f;
    std::string output_path;
    // 0 = record until RequestStop(); > 0 = stop after this many seconds.
    int duration_seconds = 0;
    int preview_width = 640;
    int preview_height = 360;
    int preview_fps = 30;
};

struct RecorderStats {
    int64_t elapsed_us = 0;
    int acquired = 0;
    int new_desktop_frames = 0;
    int encoded_frames = 0;
    int packets_written = 0;
    int64_t encoded_bytes = 0;
    int encode_errors = 0;
    int audio_packets = 0;
    int64_t audio_bytes = 0;
    int target_frames = 0;
    bool audio_track = false;
};

struct PreviewFrame {
    int width = 0;
    int height = 0;
    int stride = 0;   // bytes per row, == width * 4
    std::vector<uint8_t> bgra;
    uint64_t sequence = 0;
};

class Recorder {
public:
    Recorder();
    ~Recorder();
    Recorder(const Recorder&) = delete;
    Recorder& operator=(const Recorder&) = delete;

    // 启动工作线程并等待初始化完成；初始化失败返回 false 并写入 error。
    [[nodiscard]] bool Start(const RecorderConfig& config, std::string& error);
    // 请求在当前帧结束后停止（flush + 关闭 MP4），线程自行退出。
    void RequestStop();
    // 请求停止并等待线程退出。
    void Stop();
    bool Running() const;
    bool Finished() const;
    // 失败原因；空字符串表示尚未失败。
    std::string error() const;
    RecorderStats stats() const;
    // 返回最新预览帧；没有新帧时返回与上次相同的指针。
    std::shared_ptr<const PreviewFrame> LatestPreview() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace hmmob
```

- [ ] **Step 2: 实现**

`Recorder.cpp` 把 `src/spike/main.cpp` 现有录制循环整体搬入 `Impl::Run()`，保持行为与统计口径不变，并做以下改动：

- 线程入口：`CoInitializeEx(nullptr, COINIT_MULTITHREADED)`（成对 `CoUninitialize`）、`D3D11Device::Create()`、显示器枚举与 `display_index` 校验、`DxgiCapture::Initialize`、HDR 判定、`HdrToneMapper`、`VideoProcessor`、`AmfEncoder`、两路 `WasapiCapture`、`AudioMixer`、`AacEncoder`、`FfmpegMuxer`。任一致命失败调用 `Fail("...")` 并结束。
- 循环条件：`duration_seconds > 0` 时按原逻辑；`duration_seconds == 0` 时直到 `stop_requested`。
- 每帧判断 `stop_requested`：在当前帧编码与音频补齐后退出循环，随后执行现有停止/ flush / Close 顺序。
- `Fail(message)`：在互斥锁下写 `error_`，置 `failed_`，`RequestStop()` 等价唤醒，循环尽快退出并仍然执行 flush/Close 清理（避免线程悬挂），最后 `finished_ = true`。
- `stats()` 在互斥锁下复制 `RecorderStats`。
- `Start`：`impl_->thread = std::thread(...)` 后等待初始化结果（`std::promise<bool>` + `std::future`，或 condition_variable）；初始化成功返回 true，失败返回 false 并填写 `error`。
- 预览留到 Task 3，本任务 `LatestPreview()` 返回 `nullptr`。
- 析构调用 `Stop()`。

- [ ] **Step 3: evspike 改用 Recorder**

`src/spike/main.cpp` 保留参数解析与校验；构造 `RecorderConfig` 后：

```cpp
hmmob::Recorder recorder;
std::string error;
if (!recorder.Start(config, error)) {
    std::fprintf(stderr, "%s\n", error.c_str());
    return 1;
}
while (recorder.Running()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
}
const hmmob::RecorderStats stats = recorder.stats();
```

打印与原统计行兼容的字段（`acquired/new_desktop/encoded/packets/bytes/encode_errors/audio_packets/audio_bytes/elapsed/avg_fps`），若 `recorder.error()` 非空则输出到 stderr 并返回 1。

- [ ] **Step 4: CMake**

`hmmob_core` 增加 `src/core/recorder/Recorder.cpp`；`evspike` 无需新增链接。

- [ ] **Step 5: 构建并验证录制不回归**

```powershell
& $cmake --build build/release --config Release
.\build\release\Release\evspike.exe --output C:/temp/recorder-check.mp4 --seconds 5 --fps 60
```

预期：exit 0，统计行含 `encoded≈300`、`audio_packets>0`；`ffmpeg -v error -i C:/temp/recorder-check.mp4 -f null -` 退出码 0。再跑完整 `ctest`，预期 82/82。

- [ ] **Step 6: 提交**

```powershell
git add src/core/recorder/Recorder.h src/core/recorder/Recorder.cpp src/spike/main.cpp CMakeLists.txt
git commit -m "refactor: extract recorder engine from evspike"
```

---

### Task 3: Recorder 预览帧

**Files:**
- Modify: `src/core/recorder/Recorder.h`
- Modify: `src/core/recorder/Recorder.cpp`
- Test: `tests/test_video_processor.cpp`（仅在需要时）

- [ ] **Step 1: 实现预览**

在 `Impl` 中增加：

```cpp
hmmob::VideoProcessor preview_converter;   // BGRA 输出，缩放
Microsoft::WRL::ComPtr<ID3D11Texture2D> preview_staging;
std::shared_ptr<const hmmob::PreviewFrame> latest_preview;
uint64_t preview_sequence = 0;
int preview_interval = 1;                  // 每 N 帧生成一帧预览
```

初始化（`VideoProcessor` 与 `AMF` 之后、进入循环之前）：

- `preview_interval = max(1, fps / preview_fps)`；
- `preview_converter.Initialize(d3d, width, height, config.preview_width, config.preview_height, DXGI_FORMAT_B8G8R8A8_UNORM)`；
- 创建 `preview_staging`：尺寸 = 预览尺寸，`Format = BGRA8`，`Usage = STAGING`，`CPUAccessFlags = READ`；
- 任一失败只打印警告并关闭预览（`preview_enabled = false`），不影响录制。

循环内，在 `have_frame` 分支、编码之后（保证不阻塞主路径语义）：

```cpp
if (preview_enabled && (encoded_frames % preview_interval == 0)) {
    ID3D11Texture2D* src = tone_map_hdr ? tone_mapper.output_texture()
                                        : capture.texture();
    if (preview_converter.Convert(src, preview_converter.output_texture())) {
        d3d.context()->CopyResource(preview_staging.Get(),
                                    preview_converter.output_texture());
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (SUCCEEDED(d3d.context()->Map(preview_staging.Get(), 0,
                                         D3D11_MAP_READ, 0, &mapped))) {
            auto frame = std::make_shared<hmmob::PreviewFrame>();
            frame->width = config.preview_width;
            frame->height = config.preview_height;
            frame->stride = frame->width * 4;
            frame->bgra.resize(static_cast<size_t>(frame->height) * frame->stride);
            for (int y = 0; y < frame->height; ++y) {
                std::memcpy(frame->bgra.data() + static_cast<size_t>(y) * frame->stride,
                            static_cast<const uint8_t*>(mapped.pData) +
                                static_cast<size_t>(y) * mapped.RowPitch,
                            static_cast<size_t>(frame->stride));
            }
            d3d.context()->Unmap(preview_staging.Get(), 0);
            frame->sequence = ++preview_sequence;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                latest_preview = std::move(frame);
            }
        }
    }
}
```

`LatestPreview()`：

```cpp
std::shared_ptr<const PreviewFrame> Recorder::LatestPreview() const {
    std::lock_guard<std::mutex> lock(impl_->mutex_);
    return impl_->latest_preview;
}
```

注意：`mapped` 必须在 `Unmap` 前完成拷贝；`Map` 失败也要 `Unmap`（用作用域标志）。`preview_converter` 的输出格式与 `preview_staging` 一致。

- [ ] **Step 2: 构建并手动验证**

```powershell
& $cmake --build build/release --config Release
.\build\release\Release\evspike.exe --output C:/temp/preview-check.mp4 --seconds 5
```

预期：无预览相关错误，录制统计与之前一致；预览帧在 Task 5 由界面消费。

- [ ] **Step 3: 完整测试**

`ctest` 预期 82/82。

- [ ] **Step 4: 提交**

```powershell
git add src/core/recorder/Recorder.h src/core/recorder/Recorder.cpp
git commit -m "feat: publish scaled preview frames from the recorder"
```

---

### Task 4: Qt 程序骨架（控件 + 开始/停止 + 统计）

**Files:**
- Create: `src/app/main.cpp`
- Create: `src/app/MainWindow.h`
- Create: `src/app/MainWindow.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: CMake**

```cmake
find_package(Qt6 REQUIRED COMPONENTS Widgets)
qt_standard_project_setup()

qt_add_executable(hmmob_ev_app WIN32
  src/app/main.cpp
  src/app/MainWindow.cpp
  src/app/PreviewWidget.cpp
)
target_link_libraries(hmmob_ev_app PRIVATE hmmob_core Qt6::Widgets)
```

（`PreviewWidget` 在 Task 5 加入；若按顺序执行，本任务先不列该文件。）

部署（追加到文件末尾）：

```cmake
# Qt 插件（qwindows 等）需要随 exe 部署。
get_target_property(_qt_bin Qt6::Core IMPORTED_LOCATION_RELEASE)
get_filename_component(_qt_bin_dir "${_qt_bin}" DIRECTORY)
find_program(WINDEPLOYQT_EXE windeployqt HINTS "${_qt_bin_dir}" NO_DEFAULT_PATH)
if(WINDEPLOYQT_EXE)
  add_custom_command(TARGET hmmob_ev_app POST_BUILD
    COMMAND "${WINDEPLOYQT_EXE}" --no-translations --no-system-d3d-compiler
            --no-opengl-sw "$<TARGET_FILE:hmmob_ev_app>"
    COMMENT "Deploying Qt runtime")
endif()
```

- [ ] **Step 2: 主窗口**

`MainWindow` 继承 `QWidget`，用 `QFormLayout`/`QVBoxLayout` 组装：

- 显示器 `QComboBox`：启动时用本地 `hmmob::D3D11Device` + `EnumerateDisplays()` 填充，文本 `"0: 2560x1440"`；枚举失败则留空并禁用开始按钮。
- 帧率 `QComboBox`（30/60/120，默认 60）、编码 `QComboBox`（HEVC/H.264）、码率 `QComboBox`（20/50/100 Mbps）。
- HDR `QCheckBox`、系统声音 `QCheckBox`（默认勾选）、麦克风 `QCheckBox`（默认勾选）、系统/麦克风增益 `QDoubleSpinBox`（0.0–4.0，步进 0.1，默认 1.0）。
- 输出路径 `QLineEdit` + 「浏览…」`QPushButton`（`QFileDialog::getSaveFileName`，过滤 `MP4 (*.mp4)`，默认 `C:/temp/hmmob-<时间戳>.mp4`）。
- 「开始录制」`QPushButton`、「停止」`QPushButton`（初始禁用）。
- 状态 `QLabel`（两行：时间/帧/帧率；音频包/大小），错误 `QLabel`（红色）。
- `QTimer` 250 ms 刷新。

开始逻辑：

```cpp
hmmob::RecorderConfig config;
config.display_index = displayCombo_->currentData().toInt();
config.fps = fpsCombo_->currentData().toInt();
config.codec = codecCombo_->currentData().toString().toStdString();
config.bitrate = bitrateCombo_->currentData().toLongLong();
config.hdr = hdrCheck_->isChecked();
config.system_audio = systemCheck_->isChecked();
config.microphone = micCheck_->isChecked();
config.system_gain = static_cast<float>(systemGain_->value());
config.mic_gain = static_cast<float>(micGain_->value());
config.output_path = outputEdit_->text().toStdString();
config.duration_seconds = 0;

std::string error;
if (!recorder_.Start(config, error)) {
    ShowError(QString::fromStdString(error));
    return;
}
SetRecordingUi(true);
```

计时器：`Running()` 时刷新统计；`Finished()` 时执行一次收尾（`stats()`、恢复控件、显示错误或「已保存：路径」）。

关闭窗口：`closeEvent` 中若 `Running()`，弹 `QMessageBox::question`「正在录制，确定停止并退出？」；确定则 `recorder_.Stop()` 后接受事件。

- [ ] **Step 3: 入口**

`src/app/main.cpp`：

```cpp
#include <QApplication>
#include "MainWindow.h"

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    app.setApplicationName("Hmmob EV");
    hmmob::MainWindow window;
    window.show();
    return app.exec();
}
```

- [ ] **Step 4: 构建并启动**

```powershell
& $cmake --preset windows-x64-release   # 首次会安装 Qt 依赖
& $cmake --build build/release --config Release
.\build\release\Release\hmmob_ev_app.exe
```

预期：窗口出现，控件可用；点开始能录制，状态每秒更新；停止后文件可播放。若提示缺少 `qwindows.dll`，检查 windeployqt 是否执行、`platforms/qwindows.dll` 是否在 exe 同级 `platforms/` 下。

- [ ] **Step 5: 提交**

```powershell
git add CMakeLists.txt CMakePresets.json vcpkg.json src/app docs
git commit -m "feat: add qt recording window"
```

---

### Task 5: 预览控件与接线

**Files:**
- Create: `src/app/PreviewWidget.h`
- Create: `src/app/PreviewWidget.cpp`
- Modify: `src/app/MainWindow.h/.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: PreviewWidget**

```cpp
class PreviewWidget : public QWidget {
    Q_OBJECT
public:
    explicit PreviewWidget(QWidget* parent = nullptr);
    void SetFrame(std::shared_ptr<const hmmob::PreviewFrame> frame);
protected:
    void paintEvent(QPaintEvent* event) override;
private:
    std::shared_ptr<const hmmob::PreviewFrame> frame_;
    QImage image_;
};
```

`SetFrame`：保存 `frame_`，用其 `bgra.data()` 构造不拥有内存的 `QImage(frame->bgra.data(), w, h, stride, QImage::Format_ARGB32)`，`update()`。`frame_` 必须在 `image_` 之后重置，保证内存生命周期。

`paintEvent`：填充深色背景；按控件尺寸等比缩放绘制 `image_`；无帧时绘制「预览未开始」。

- [ ] **Step 2: 接线**

`MainWindow` 增加 `PreviewWidget* preview_`，放在控件区上方，最小尺寸建议 480×270。计时器里：

```cpp
auto frame = recorder_.LatestPreview();
if (frame && frame != last_preview_) {
    last_preview_ = frame;
    preview_->SetFrame(frame);
}
```

- [ ] **Step 3: 构建并验证预览**

启动界面 → 开始录制 → 预览区应出现桌面画面（约 30 fps）；停止后画面保留最后一帧。

- [ ] **Step 4: 提交**

```powershell
git add src/app/PreviewWidget.h src/app/PreviewWidget.cpp src/app/MainWindow.h src/app/MainWindow.cpp CMakeLists.txt
git commit -m "feat: show live preview in the recording window"
```

---

### Task 6: 收尾与文档

**Files:**
- Modify: `README.md`
- Modify: `docs/superpowers/specs/2026-11-07-hmmob-ev-design.md`（仅在需要记录预览方案偏差时）

- [ ] **Step 1: README**

新增「图形界面」一节：

```markdown
## 图形界面

```powershell
.\build\release\Release\hmmob_ev_app.exe
```

可选择显示器、帧率、编码器与码率，开关系统声音/麦克风并调整增益，选择保存路径后开始/停止录制；界面含实时预览（640×360，约 30 fps）与录制统计。

> 预览通过 GPU 缩放到 640×360 后读回 CPU 绘制，不是最终版的低延迟 SwapChain 预览。
```

并在前置条件中加上 Qt 由 vcpkg 自动安装。

- [ ] **Step 2: 完整验证**

```powershell
& $cmake --build build/release --config Release
& $ctest --test-dir build/release -C Release --output-on-failure
.\build\release\Release\hmmob_ev_app.exe   # 手动：录 10 秒，停止，播放文件
```

记录：测试总数、界面录制统计、`ffmpeg -i` 结果。

- [ ] **Step 3: 提交**

```powershell
git add README.md docs
git commit -m "docs: add gui usage notes"
```

---

## 风险与注意事项

1. **Qt 部署**：`windeployqt` 必须找到 vcpkg 的 Qt bin 目录；构建后检查 `platforms/qwindows.dll`。缺失时窗口无法创建（报 `could not find or load the Qt platform plugin "windows"`）。
2. **D3D11 线程**：UI 线程只做短生命周期的显示器枚举（自己的 `D3D11Device`），录制设备完全属于 Recorder 工作线程；不要把 Recorder 的设备传给 UI。
3. **预览读回**：`Map` 是阻塞读回，只在预览间隔执行；预览失败只警告，不影响录制。
4. **COM**：工作线程内 `CoInitializeEx(MTA)`，`AmfEncoder` 与 WASAPI 都在该线程使用；UI 线程由 Qt 自己初始化 COM（`CoInitializeEx(STA)`）。
5. **路径编码**：Qt 的 `QString::toStdString()` 为 UTF-8，FFmpeg 接受 UTF-8；`evspike` 的 ANSI 限制只影响命令行。
6. **停止语义**：`RequestStop` 必须让循环完成当前帧的编码与音频补齐，再执行 flush/Close；避免截断 MP4。
