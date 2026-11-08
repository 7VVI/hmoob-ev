# 多源采集（全屏/区域/窗口/摄像头）实施计划

**Goal:** 让 `hmmob_ev_app` 的四种录制模式都有真实实现，并修正界面细节。

**Architecture:** 引入 `hmmob::VideoSource` 抽象。Recorder 只依赖该接口，具体实现：

| 模式 | 实现 |
| --- | --- |
| 屏幕 | `DesktopSource`（DXGI Desktop Duplication，现有逻辑） |
| 区域 | `DesktopSource` + `region`；`VideoProcessor` 用源矩形裁剪到区域尺寸后编码 |
| 窗口 | `WindowSource`（Windows Graphics Capture，选窗口 HWND） |
| 摄像头 | `CameraSource`（Media Foundation SourceReader，默认 RGB32） |

**Tech Stack:** C++20、D3D11、DXGI、WGC（Win10 1903+）、Media Foundation、AMF、FFmpeg、Qt6 Widgets。

---

## Task A: VideoSource 抽象 + 区域录制

**Files:**
- Create `src/core/capture/VideoSource.h`
- Create `src/core/capture/DesktopSource.h/.cpp`
- Modify `src/core/gfx/VideoProcessor.h/.cpp`（增加带源矩形的 Convert）
- Modify `src/core/recorder/Recorder.cpp`（改用 VideoSource）
- Modify `src/core/recorder/Recorder.h`（RecorderConfig 增加 mode/region/window/camera 字段）
- Test `tests/test_video_processor.cpp`（裁剪测试）、`tests/test_recorder.cpp`（区域录制集成）

接口要点：

```cpp
enum class CaptureMode { Screen, Region, Window, Camera };

struct SourceFrame {
    ID3D11Texture2D* texture = nullptr;
    int width = 0;              // 输出（编码）尺寸
    int height = 0;
    RECT source_rect{};         // texture 内的有效区域（区域模式）
    bool new_frame = false;
    bool hdr = false;
};

class VideoSource {
public:
    virtual ~VideoSource() = default;
    virtual bool Initialize(D3D11Device& d3d, const RecorderConfig& cfg,
                            std::string& error) = 0;
    virtual bool Acquire(SourceFrame& frame, int timeout_ms) = 0;
    virtual int width() const = 0;
    virtual int height() const = 0;
    virtual DXGI_COLOR_SPACE_TYPE color_space() const = 0;
    virtual bool hdr_capable() const = 0;
    virtual bool hdr_downgraded() const { return false; }
};
```

- `DesktopSource`: 封装 `DxgiCapture`；`Initialize` 按 `cfg.display_index/cfg.hdr`；区域模式下 `width/height` = 区域尺寸，`source_rect` = 区域；`Acquire` 透传 `CaptureFrame`。
- `VideoProcessor::Convert(input, output, const RECT* source_rect)`: 设置 `D3D11_VIDEO_PROCESSOR_STREAM.SourceRect/DestRect`，校验矩形在输入范围内；原 `Convert` 委托 nullptr。
- Recorder：持有 `std::unique_ptr<VideoSource>`；预览（Recorder 内与 DesktopPreview）在区域模式下裁剪同一矩形；失败信息保持原样。屏幕模式行为不变。

验收：`ctest` 全绿；`--smoke` 录屏不变；区域录制在 640×360@(100,100) 下输出 640×360 的可解码 MP4。

---

## Task B: 窗口录制（WGC）

**Files:**
- Create `src/core/capture/WindowSource.h/.cpp`
- Modify `Recorder.cpp`（按 mode 构造 source）
- UI：选窗口对话框（列出可见、有标题的顶层窗口）

要点：
- `IGraphicsCaptureItemInterop::CreateForWindow(hwnd)` 创建 item；`Direct3D11CaptureFramePool::CreateFreeThreaded(device, BGRA8, 2, item.Size())`；`StartCapture()`。
- `TryGetNextFrame()` 轮询；`Surface` 经 `IDirect3DDxgiInterfaceAccess` 取 `ID3D11Texture2D`；关闭光标捕获按需；窗口尺寸变化时返回明确错误（编码分辨率不可中途改变）。
- 录制的 `width/height` 取 item.Size；SDR BGRA，无 HDR。

验收：选一个窗口录制 3 秒，输出尺寸等于窗口内容尺寸，可解码；窗口关闭/尺寸变化时以错误结束而非坏文件。

---

## Task C: 摄像头（Media Foundation）

**Files:**
- Create `src/core/capture/CameraSource.h/.cpp`
- UI：高级设置中的摄像头设备下拉（枚举 `MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDEOCAPTURE`）

要点：
- `MFCreateDeviceSource` + `MFCreateSourceReaderFromMediaSource`；请求 RGB32（必要时 NV12→RGB32 由 MF 转换），固定 30 fps，保留原生分辨率（上限 1920×1080）。
- `ReadSample` 循环在采集线程；只保留最新帧；`Acquire` 在没有新帧时复用上一帧纹理（录制节奏 60 fps）。
- 每帧 CPU 上传到 D3D11（`UpdateSubresource` 到默认纹理），之后走 `VideoProcessor` BGRA→NV12。
- 停止时正确 `Shutdown`、释放 reader/source。

验收：默认摄像头录制 3 秒，输出尺寸/帧率正确、可解码；无摄像头时界面给出可读错误。

---

## Task D: 界面接线与修正

**Files:** `src/app/MainWindow.cpp/.h`、`ModeTile`、`IconRenderer`、新增 `RegionSelector`、`WindowPickerDialog`。

- 四个磁贴均可选；开始录制时：
  - 全屏：现有路径。
  - 区域：显示全屏半透明遮罩，拖拽框选 + 确认，Esc 取消；区域传给 RecorderConfig。
  - 窗口：弹出窗口列表（图标+标题），选择后传入 HWND。
  - 摄像头：使用高级设置选中的设备（默认第一个）。
- 空闲预览：全屏/区域继续用 DesktopPreview（区域在预览上画选框虚边）；窗口/摄像头在未录制时显示「开始录制后显示画面」占位。
- 右上「打开文件」与保存路径文件夹按钮：`QDesktopServices::openUrl(QUrl::fromLocalFile(目录))`。
- 删除编码格式右侧的「ⓘ」。
- 「预计时长 / 预计大小」改为图标与数值同一行对齐（caption+value 的基线一致）。
- 模式切换时重置选择、更新状态文案。

验收：四种模式各自录制 3 秒均有可解码 MP4；截图与参考图保持一致；`ctest` 全绿。
