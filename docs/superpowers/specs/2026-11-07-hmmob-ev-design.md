# Hmmob EV 设计文档

- 日期：2026-11-07
- 状态：已评审（2026-11-07）
- 项目目录：`C:\Users\Administrator\Desktop\project\hmmob-ev`
- 产品定位：Windows 端高性能录屏软件

---

## 1. 背景与目标

本项目的技术路线来自对 EVCapture（EV 录屏）5.4.9 安装目录的 PE 导入表、导出符号和字符串分析。EV 的核心不是简单调用 FFmpeg，而是一套完整的：

```
DXGI/WGC 采集 → D3D11 合成 → 硬件/软件编码 → FFmpeg 封装
```

Hmmob EV 沿用这套分层思想，但代码全部自研，不复制 EV 的任何代码或资源。

### 1.1 产品目标（V1 完整范围）

> 交付节奏见第 8 节：Phase 2 结束形成可交付的核心 V1；Phase 3/4 作为 V1.x 增量。

- Windows 10 1809+ / Windows 11，x64。
- 全屏、区域、窗口、仅摄像头四种录制模式。
- 4K 60fps；HDR 显示器输入，输出 SDR MP4。
- 完整叠加层：摄像头画中画、水印、鼠标、标注/画笔。
- 系统声音 + 麦克风采集与混音。
- NVIDIA NVENC / Intel QSV / AMD AMF 硬件编码，软件编码兜底。
- 暂停/继续、倒计时、定时/分段录制。
- 全局快捷键、系统托盘、录制悬浮条。
- 录制后预览、首尾裁剪、片段合并。
- RTMP 推流（同一路编码同时本地录制 + 推流）。
- 崩溃安全：异常退出后文件可恢复。

### 1.2 V1 不做

- macOS / Linux。
- 自建账号、云存储、分享链接、CDN（分享上传不在本项目范围）。
- 多轨道时间线、转场、字幕、复杂特效。
- 8K、多 GPU 同时编码、HDR 视频输出。
- 移动端、Web 端。
- 自动更新、遥测。

### 1.3 成功标准（完整 V1）

- 4K60 连续录制 10 分钟无丢帧、无花屏。
- 音画同步误差小于 50ms。
- HDR 显示器录制不偏色、不过曝，SDR 播放器正常。
- 强制结束进程后，临时文件可播放并能在重启后转换为 MP4。
- 区域/窗口/多显示器/DPI 缩放场景下，选区与录制画面一致。
- RTMP 推流 30 分钟稳定，断线重连不影响本地录制。

---

## 2. 参考：EVCapture 技术分析

以下结论来自对安装目录的静态分析，不是反编译源码。用于理解架构，不作为代码来源。

### 2.1 模块职责

| 模块 | 职责 | 关键证据 |
|---|---|---|
| `EVCapture.exe` | Qt5 主程序、业务逻辑、WASAPI 音频、DirectShow 摄像头、GDI 兜底 | 导入 Qt5Widgets/Network、mmdevapi、GDI32、avcodec；字符串 WASAPI/Loopback/Mic/DShow/Camera |
| `mcore.dll` | DXGI 桌面复制、D3D11 合成、音频混音、显示器枚举 | 导出 `GsScreenDup`、`GsScene`、`GsView`、`GsItem`、`GsTexture`、`GsFilter`、`McAudioMixer`；导入 dxgi/d3d11/D3DCompiler_47/AVRT |
| `mc_winrt.dll` | Windows Graphics Capture 窗口采集 | `GraphicsCaptureItem`、`Direct3D11CaptureFramePool`、`GraphicsCaptureSession`、`IsCursorCaptureEnabled` |
| `evc_video_encoder.dll` | 编码器抽象 | `EvcVideoEncoder`、`EvcSwEncoder`、`EvcGpuEncoder`、`EvcEncoderPacket` |
| `evc_nvenc.dll` | NVIDIA NVENC | `nvEncInitializeEncoder`、`NV_ENC_*`、H.264/HEVC |
| `evc_qsv11.dll` | Intel Quick Sync | `MFX_*`、`mfxImplDescription`、QSV |
| `evc_amf.dll` | AMD AMF | `AMFInit`、`AMFComponent`、`amf_create_encoder` |
| `FLAVLib.dll` | FFmpeg 封装/写文件/修复 | 导出 `FLAV_start`、`FLAV_write_video_frame`、`FLAV_write_audio_pcm`、`FLAV_StartRepair`；导入 avformat/avcodec |
| `AVDecoder.dll` | FFmpeg 解码 | `FFDecoder`、`AudioMixer`、`getVideoFrame` |
| `mediaserver.exe` | 本地流媒体服务 | RTMP/HLS/FLV/WebSocket 字符串；产品名“EV录屏播放服务器” |
| `win-hook.dll` | 全局键鼠钩子 | 导出 `install_hook`、`add_key_hook_proc` |
| `Bridge.dll` | 配置/日志/共享内存/IPC 基础设施 | `bg::InitBridge`、`LoadModules`、共享内存、包池 |
| `EVNetLib.dll` | HTTP/2 网络、账号、上传 | 导入 nghttp2、WS2_32、bcrypt |
| `Mp4FixDll.dll` | MP4 崩溃修复 | 模块名与 FLAV 修复接口 |

### 2.2 EV 的录制链路

1. **采集**：`GsScreenDup` 使用 DXGI Desktop Duplication（`DuplicateOutput` / `DuplicateOutput1` / `AcquireNextFrame`）；窗口用 WGC；老系统用 GDI。
2. **合成**：`GsScene` 场景图，桌面纹理做背景，摄像头/水印/鼠标/标注作为 `GsItem`，`GsView` 处理裁剪、缩放、预览，`GsDevice` 按 FPS 驱动渲染。
3. **编码**：`EvcVideoEncoder` 统一接口，软件走 FFmpeg libavcodec，硬件走 NVENC/QSV/AMF 插件。
4. **封装**：`FLAVLib` 调 libavformat 写 MP4/AVI/FLV/WMV/MKV/MOV/MP3，也支持 RTMP。
5. **音频**：WASAPI Loopback + 麦克风，`McAudioMixer` 混音，FFmpeg AAC 编码。
6. **容错**：DXGI 访问丢失重建、编码器回退、崩溃后 MP4 修复。

### 2.3 借鉴与不借鉴

借鉴：分层架构、零拷贝 GPU 管线、编码器抽象、场景图合成、音频主时钟、崩溃恢复思路。

不借鉴：EV 的私有实现、UI 资源、图标、文案、协议格式；不复用其二进制文件。

---

## 3. 需求

### 3.1 功能需求

| 编号 | 需求 | 优先级 |
|---|---|---|
| FR-01 | 全屏录制（指定显示器） | P0 |
| FR-02 | 区域录制（拖拽选区、窗口吸附） | P0 |
| FR-03 | 窗口录制（WGC，窗口被遮挡仍可录） | P0 |
| FR-04 | 仅摄像头录制 | P1 |
| FR-05 | 系统声音 + 麦克风，独立音量/静音 | P0 |
| FR-06 | 摄像头画中画：位置、大小、圆角、镜像 | P0 |
| FR-07 | 水印：图片、位置、大小、透明度 | P1 |
| FR-08 | 鼠标光标绘制，可选高亮/点击效果 | P1 |
| FR-09 | 标注：画笔、箭头、矩形、橡皮、清空 | P1 |
| FR-10 | 暂停/继续、倒计时、定时录制 | P0 |
| FR-11 | 按大小/时长自动分段 | P1 |
| FR-12 | 全局快捷键、托盘、悬浮控制条 | P0 |
| FR-13 | 4K60 硬件编码，软件编码兜底 | P0 |
| FR-14 | HDR 输入、SDR 输出 | P0 |
| FR-15 | MP4 输出；崩溃后文件可恢复 | P0 |
| FR-16 | 录制后预览、首尾裁剪、片段合并 | P1 |
| FR-17 | RTMP 推流，支持录制+推流并行 | P1 |
| FR-18 | 设置持久化、文件库、打开所在目录 | P0 |
| FR-19 | 多显示器、DPI 缩放、设备热插拔 | P0 |
| FR-20 | 中文界面；英文预留 | P1 |

### 3.2 非功能需求

| 编号 | 要求 |
|---|---|
| NFR-01 | 4K60 录制时，GPU 编码路径不得出现 CPU 回读 |
| NFR-02 | 音画同步误差 < 50ms；暂停恢复后时间轴连续 |
| NFR-03 | 录制中 UI 不卡顿；悬浮条响应 < 100ms |
| NFR-04 | 编码队列有界；队列满时记录丢帧并告警，不无限占内存 |
| NFR-05 | 硬件编码失败自动回退，录制不中断 |
| NFR-06 | 崩溃后临时文件可播放；重启后可恢复为 MP4 |
| NFR-07 | 安装包不依赖额外运行库（VC 运行库静态链接或随包） |
| NFR-08 | 默认不联网、不上传任何数据 |
| NFR-09 | 代码中不引入 GPL 组件；FFmpeg 使用 LGPL 构建 |
| NFR-10 | 录制文件命名可配置，路径支持中文/长路径 |

### 3.3 平台矩阵

- 操作系统：Windows 10 1809+、Windows 11；x64。
- 不支持 Windows 7/8（EV 有 GDI 兜底，本项目第一版不做）。
- 显卡：NVIDIA（NVENC）、Intel（QSV）、AMD（AMF）；无硬件编码时软件兜底，限制 1080p。
- HDR：Windows 10 1709+，HDR 显示器，DXGI `DuplicateOutput1` 可用。
- 开发工具链：MSVC 2022 BuildTools 14.44、CMake 3.29+、Ninja。

---

## 4. 架构

### 4.1 总览

```
hmmob-ev.exe
├─ app/              Qt 应用入口、单实例、生命周期
├─ ui/               主窗口、区域选择器、悬浮条、设置、文件库
├─ preview/          D3D11 子窗口预览、播放器
├─ editor/           裁剪、合并（Phase 3）
├─ platform/         Win32、DPI、显示器、快捷键、开机启动
├─ services/         配置、日志、崩溃转储、路径
└─ core/             hmmob_core 录制引擎
    ├─ capture/      DXGI、WGC、GDI、Media Foundation、WASAPI
    ├─ compositor/   D3D11 设备、场景、图层、着色器、颜色转换
    ├─ encoder/      NVENC、QSV、AMF、软件编码
    ├─ audio/        混音、AAC 编码
    ├─ muxer/        FFmpeg 封装、RTMP、恢复
    └─ engine/       状态机、时钟、队列、错误恢复
```

第一版采用**单进程 + 模块化静态库**，与 EV 的进程模型一致。`hmmob_core` 通过纯 C++ 接口暴露给 UI，不依赖 Qt，便于后续抽出独立录制进程或 SDK。

### 4.2 线程模型

| 线程 | 职责 | 备注 |
|---|---|---|
| UI 线程 | Qt 事件循环、窗口、设置 | 不参与采集/编码 |
| Capture 线程 | DXGI/WGC 取帧、摄像头取帧 | WGC 用回调/帧池 |
| Render 线程 | 按 FPS 合成、颜色转换 | 持有 D3D11 上下文 |
| Video Encoder 线程 | 提交纹理、取码流 | 每编码器一个 |
| Audio 线程 | WASAPI 采集、混音、AAC | 实时优先级 |
| Muxer 线程 | 写 MP4/MKV/FLV、RTMP | 磁盘/网络 IO 不阻塞编码 |
| Preview 线程 | 预览交换链呈现 | 可降帧率 |

队列：

- 采集→合成：3~5 帧纹理环形队列。
- 合成→编码：2~3 帧，编码器输入纹理池。
- 编码→封装：码流包队列，有界（例如 256 包）。
- 音频→封装：PCM 环形缓冲 + AAC 包队列。

所有队列满时的策略必须显式定义：视频丢最旧帧并计数；音频不丢，靠背压；封装队列满则暂停对应编码器。

### 4.3 核心接口（设计级）

```cpp
struct VideoFrame {
    ID3D11Texture2D* texture;
    int64_t pts_us;
    int width, height;
    bool is_hdr;
};

class IScreenCapture {
public:
    virtual ~IScreenCapture() = default;
    virtual bool Start(const CaptureConfig& cfg) = 0;
    virtual bool Acquire(VideoFrame& out, int timeout_ms) = 0;
    virtual void Stop() = 0;
};

class ICompositor {
public:
    virtual ~ICompositor() = default;
    virtual bool Render(const VideoFrame& screen,
                        const OverlayState& overlays,
                        ID3D11Texture2D** out) = 0;
};

class IVideoEncoder {
public:
    virtual ~IVideoEncoder() = default;
    virtual bool Configure(const VideoEncoderConfig& cfg) = 0;
    virtual bool Encode(ID3D11Texture2D* texture, int64_t pts_us) = 0;
    virtual bool ReceivePacket(EncodedPacket& out) = 0;
    virtual void Flush() = 0;
};

class IAudioSource {
public:
    virtual ~IAudioSource() = default;
    virtual bool Start(const AudioConfig& cfg) = 0;
    virtual bool Read(float* dst, int frames, int timeout_ms) = 0;
};

class IMuxer {
public:
    virtual ~IMuxer() = default;
    virtual bool Open(const MuxerConfig& cfg) = 0;
    virtual bool WriteVideo(const EncodedPacket& pkt) = 0;
    virtual bool WriteAudio(const EncodedPacket& pkt) = 0;
    virtual bool Close() = 0;
};

class RecordingEngine {
public:
    bool Start(const RecordingConfig& cfg);
    void Pause();
    void Resume();
    void Stop();
    EngineStats Stats() const;
};
```

### 4.4 错误处理原则

- 采集失败：DXGI 访问丢失/分辨率变化 → 重建采集对象，最多重试 3 次；失败则停止录制并保留已写文件。
- 编码失败：硬件编码器初始化/运行失败 → 切换软件编码；软件编码也失败 → 停止并提示。
- 音频失败：设备独占/被拔出 → 静音该路并继续；默认设备变化 → 重建采集。
- 封装失败：磁盘满/网络断 → 停止对应输出；本地录制优先于 RTMP。
- 所有错误写入日志，UI 显示可读错误码。

---

## 5. 关键流程

### 5.1 启动与设备探测

1. 创建 D3D11 设备（`D3D11CreateDevice`，`BGRA_SUPPORT`，硬件优先）。
2. 枚举显示器（`IDXGIFactory1::EnumAdapters1` + `EnumOutputs`），标记 HDR、主显示器、DPI。
3. 探测编码器：按 GPU 厂商加载 NVENC/QSV/AMF；查询 4K60/H.264/HEVC 能力；记录可用性。
4. 枚举摄像头（Media Foundation）和音频设备（WASAPI）。
5. 加载配置，恢复上次录制模式。

### 5.2 开始录制

1. 校验输出路径、磁盘空间、编码器能力。
2. 初始化采集源、合成器、编码器、音频源、封装器。
3. 启动音频采集（先建立音频时钟）。
4. 启动采集线程、渲染线程、编码线程、封装线程。
5. UI 进入录制状态，显示悬浮条和计时。

### 5.3 帧采集与合成

- DXGI：`AcquireNextFrame(timeout)` → 取 `IDXGIResource` → `QueryInterface(ID3D11Texture2D)` → 复制/引用到纹理池 → `ReleaseFrame`。
- 超时（桌面无变化）：复用上一帧纹理，但按目标 FPS 继续输出。
- WGC：`Direct3D11CaptureFramePool::TryGetNextFrame` → 取 `Direct3D11CaptureFrame` → 纹理 → `Close`。
- 摄像头：Media Foundation `IMFSourceReader::ReadSample` → NV12/RGB32 → 上传纹理。
- 合成器按 `target_fps` 定时渲染；如果采集帧率高于目标 FPS，跳过旧帧；低于目标 FPS，重复上一帧。
- 合成结果输出到 BGRA8 纹理；HDR 输入在着色器内完成色调映射。

### 5.4 编码与封装

- 合成纹理 → `ID3D11VideoProcessor` 转 NV12（8bit）→ 编码器。
- 编码器输出 `EncodedPacket { data, size, pts_us, dts_us, keyframe }`。
- 封装线程从队列取包，`av_interleaved_write_frame` 写入 MKV/MP4/FLV。
- 停止时：flush 编码器 → 写文件尾 → 关封装器 → 临时 MKV remux 为 MP4（可选保留 MKV）。

### 5.5 音频

- 系统声：默认渲染端点 + `AUDCLNT_STREAMFLAGS_LOOPBACK`。
- 麦克风：默认采集端点。
- 统一转 float32、48kHz、立体声；混音后做软限幅。
- AAC-LC 192kbps；PTS 由累计采样数计算。
- 音频为主时钟；视频 PTS 与音频时钟比较，必要时重复/丢弃视频帧。

### 5.6 暂停/恢复

- 暂停：停止采集和编码，flush 编码器；记录暂停开始时间。
- 恢复：从暂停时长中扣除时间偏移，继续写包；文件时间轴不出现空洞。
- 暂停期间音频不写入；恢复后从当前采样继续。

### 5.7 停止与崩溃恢复

- 正常停止：flush → 写索引 → 关闭 → remux MP4。
- 异常退出：临时 MKV 仍在磁盘；启动时扫描未完成任务，提示恢复并 remux。
- 临时文件命名：`<output>.recording.mkv`；完成后原子重命名/转换。
- 磁盘空间低于 500MB 时自动停止并提示。

### 5.8 RTMP 并行

- 编码器输出码流 tee 到两个封装器：本地 MKV/MP4 和 FLV/RTMP。
- 推流参数：URL + 流密钥、码率、分辨率、FPS；第一版与本地录制一致。
- 推流失败：本地录制继续；后台重连（1s/2s/4s/8s，上限 30s）。
- 用户可选择“仅录制”“仅推流”“录制+推流”。

### 5.9 区域/窗口选择

- 区域选择器：每个显示器一个无边框置顶窗口，半透明遮罩；拖拽选区，显示尺寸；窗口高亮吸附；放大镜；Enter 确认、Esc 取消。
- 选区坐标统一转换为虚拟桌面坐标，处理负坐标和多显示器 DPI。
- DXGI 路径：采集整屏后按选区裁剪/缩放；WGC 路径：窗口直接采集，区域模式采集显示器后裁剪。

---

## 6. 关键技术设计

### 6.1 DXGI Desktop Duplication

- 使用 `IDXGIOutput1::DuplicateOutput`；HDR 优先 `IDXGIOutput5::DuplicateOutput1`，请求 `DXGI_FORMAT_R16G16B16A16_FLOAT` 和 `DXGI_FORMAT_R10G10B10A2_UNORM`。
- `DXGI_OUTDUPL_FRAME_INFO` 提供 `LastPresentTime`、`AccumulatedFrames`、`PointerPosition`、`PointerShapeBufferSize`。
- 光标：读取 `PointerShapeBuffer`，解析彩色/单色掩码，合成时绘制。
- 错误处理：
  - `DXGI_ERROR_WAIT_TIMEOUT`：无新帧，复用上一帧。
  - `DXGI_ERROR_ACCESS_LOST`：分辨率/显示器/会话变化，释放并重建。
  - `DXGI_ERROR_INVALID_CALL`：参数/状态错误，记录并停止。
- 纹理池：每个显示器 3~5 张 `D3D11_USAGE_DEFAULT` 纹理，避免频繁分配。

### 6.2 Windows Graphics Capture

- `GraphicsCaptureItem`（窗口/显示器）→ `Direct3D11CaptureFramePool::CreateFreeThreaded` → `GraphicsCaptureSession::StartCapture`。
- `IsCursorCaptureEnabled(false)`，光标由合成器绘制，保证样式一致。
- `IsBorderRequired`（Win11）：默认关闭边框；系统要求时提示用户授权。
- 受保护内容/DRM 窗口可能黑屏，捕获失败时提示。
- WGC 帧时间戳使用 `SystemRelativeTime`，转换为 QPC/微秒。

### 6.3 GDI 兜底

第一版不做 Windows 7 支持，GDI 仅作为 DXGI/WGC 失败时的降级路径：

- `BitBlt` 到内存 DC → `GetDIBits` → 上传纹理。
- 限制 1080p30；不保证 HDR；用于异常诊断，不作为主路径。

### 6.4 D3D11 合成器

- 单 `ID3D11Device` + `ID3D11DeviceContext`，开启 `ID3D11Multithread::SetMultithreadProtected(TRUE)`。
- 渲染目标：`D3D11_USAGE_DEFAULT` 的 BGRA8 纹理，绑定 RTV。
- 图层：
  1. 桌面纹理：按选区裁剪/缩放，支持旋转（0/90/180/270）。
  2. 摄像头：纹理、圆角、阴影、镜像、边框。
  3. 水印：PNG/SVG 纹理，alpha 混合。
  4. 鼠标：指针纹理 + 位置；可选点击涟漪/高亮。
  5. 标注：透明纹理，画笔/箭头/矩形，撤销/清空。
- 着色器：
  - 顶点：全屏三角形/四边形。
  - 像素：纹理采样、alpha 混合、圆角 SDF、HDR 色调映射。
  - 缩放：双线性/双三次；4K 缩放优先 `ID3D11VideoProcessor`。
- 输出：BGRA8 纹理，交给 `ID3D11VideoProcessor` 转 NV12。

### 6.5 HDR → SDR 色调映射

- 检测：`IDXGIOutput6::GetDesc1` 的 `ColorSpace`：
  - `DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020`：HDR10（PQ/BT.2020）。
  - `DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709`：scRGB 线性。
  - 其余按 SDR 处理。
- 读取 `MaxLuminance`、`MaxFullFrameLuminance`、色度坐标。
- 着色器：
  1. PQ/HLG EOTF → 线性光。
  2. BT.2020 → BT.709 色域转换（3x3 矩阵）。
  3. 按 SDR 白点（默认 203 nits，可配置 80~300）缩放。
  4. 高光压缩（Reinhard/ACES 简化版），避免过曝。
  5. sRGB OETF → 8bit。
- 输出 MP4 不写 HDR 元数据；播放器按 SDR 显示。
- 无 HDR 格式时退回 SDR 采集，UI 提示。

### 6.6 颜色转换与编码器输入

- 优先 `ID3D11VideoProcessor`：
  - `VideoProcessorSetOutputColorSpace`、`VideoProcessorSetStreamColorSpace`。
  - BGRA8 → NV12（H.264/HEVC 8bit）。
- 备选：计算着色器/像素着色器做 YUV 转换，但优先 VideoProcessor。
- 编码器输入纹理必须是 `D3D11_BIND_RENDER_TARGET` 或 `D3D11_BIND_VIDEO_ENCODER`（按 SDK 要求）。
- 禁止在 4K60 路径使用 `Map` 回读。

### 6.7 音频采集、混音与同步

- WASAPI 共享模式、事件驱动；`IAudioClient::Initialize` 使用 `AUDCLNT_STREAMFLAGS_LOOPBACK`（系统声）或普通采集（麦克风）。
- 格式：float32、48kHz、立体声；设备不支持时用 `IAudioClient::IsFormatSupported` + `GetMixFormat` 转换。
- 混音：逐样本相加，软限幅；每路音量/静音；可选高通滤波去直流。
- 设备变化：`IMMNotificationClient::OnDefaultDeviceChanged` → 重建对应采集。
- 同步：音频累计采样数 → 主时钟；视频帧 PTS 与主时钟对齐；偏差超过 100ms 时重复/丢帧。

### 6.8 编码器抽象与回退

- 接口 `IVideoEncoder`，实现：
  - `NvencEncoder`：NVENC SDK，H.264/HEVC，NV12 输入，CBR/VBR，GOP 2s。
  - `QsvEncoder`：oneVPL/MFX，H.264/HEVC，NV12 输入。
  - `AmfEncoder`：AMF，H.264/HEVC，NV12 输入。
  - `SwEncoder`：OpenH264 或 FFmpeg 原生 H.264，1080p 上限。
- 选择顺序：用户指定 → 当前 GPU 厂商 → 其他可用硬件 → 软件。
- 能力探测：分辨率、FPS、码率范围、H.264/HEVC、10bit（第一版输出 8bit）。
- 回退：初始化失败/运行中连续错误 → 切换到下一个编码器；切换时保留时间轴，必要时插入关键帧。
- 配置默认值：
  - 4K60：HEVC 40~80Mbps 或 H.264 50~100Mbps，CBR。
  - 1080p60：8~16Mbps。
  - 1080p30：4~8Mbps。
  - GOP = 2 × FPS；B 帧默认关闭（录屏/推流兼容性）。

### 6.9 封装与崩溃安全

- `FfmpegMuxer` 封装 libavformat：
  - 本地：MKV（临时）→ MP4（remux，`+faststart`）。
  - 分段：按大小/时长关闭当前文件并开新文件。
  - RTMP：FLV muxer，`rtmp://` 输出。
- 时间基：视频/音频统一 `1/1000000`（微秒），写出前 rescale 到流时间基。
- 崩溃安全：
  - 录制中写 `.recording.mkv`；MKV 对未收尾文件容忍度高。
  - 正常停止：flush → 写 trailer → 关闭 → remux MP4。
  - 异常退出：启动时扫描 `.recording.mkv`，提示恢复；能 remux 则转 MP4，不能则保留 MKV。
- 可选“直接 MP4”模式，用于对文件格式有强需求的用户；崩溃修复能力较弱，UI 明确说明。

### 6.10 RTMP

- FFmpeg `flv` muxer + `rtmp://` 协议；`avio_open2` 设置超时。
- 编码参数：H.264 + AAC，CBR，GOP 2s，profile main/high，level 自动。
- 推流线程独立；网络阻塞不影响本地封装。
- 断线重连：指数退避，最多 30s；重连后从下一个关键帧开始（FLV 需要关键帧对齐）。
- 第一版录制与推流共用编码器；不同参数时提示，二期支持双编码。

### 6.11 预览

- 预览窗口使用原生子 HWND + D3D11 SwapChain（`DXGI_SWAP_EFFECT_FLIP_DISCARD`），避免 QWidget CPU 绘制。
- 合成器把最终帧 Blit 到预览交换链；预览可降帧率（30fps）以省 GPU。
- 播放器：FFmpeg 解码 → D3D11 纹理 → 同一预览窗口；支持播放/暂停/进度/音量。
- 编辑预览（Phase 3）：时间线、缩略图、裁剪入点/出点。

### 6.12 快捷键、托盘、悬浮条

- 全局快捷键：`RegisterHotKey`（默认 Ctrl+Alt+R 开始/停止，Ctrl+Alt+P 暂停）；冲突时提示。
- 托盘：开始/停止、暂停、打开文件库、设置、退出。
- 悬浮条：置顶、可拖动、半透明；`SetWindowDisplayAffinity(WDA_EXCLUDEFROMCAPTURE)` 排除录制。
- 单实例：命名互斥体；第二次启动唤起主窗口。

### 6.13 配置、日志、崩溃

- 配置：JSON（`%APPDATA%\HmmobEV\config.json`），带版本号；损坏时备份并重建。
- 日志：按天滚动，保留 7 天；记录设备、编码器、错误码、丢帧统计。
- 崩溃：`SetUnhandledExceptionFilter` + `MiniDumpWriteDump`，转储到 `%LOCALAPPDATA%\HmmobEV\crashes`。
- 隐私：默认不联网；日志不包含画面/音频内容；除输出文件路径外，不记录用户隐私数据。

### 6.14 权限与安全

- 默认普通用户权限；不使用全局低级键鼠钩子（避免杀软误报）。
- 捕获受保护内容/DRM 窗口时明确提示“无法录制”。
- 文件写入用户选择的目录；长路径使用 `\\?\` 前缀。
- 安装包签名（Phase 2）。

---

## 7. 工程结构与构建

### 7.1 目录

```
hmmob-ev/
├─ CMakeLists.txt
├─ CMakePresets.json
├─ vcpkg.json
├─ cmake/
├─ src/
│  ├─ app/
│  ├─ ui/
│  ├─ preview/
│  ├─ editor/
│  ├─ platform/
│  ├─ services/
│  └─ core/
│     ├─ capture/
│     ├─ compositor/
│     ├─ encoder/
│     ├─ audio/
│     ├─ muxer/
│     └─ engine/
├─ shaders/
├─ resources/
├─ tests/
├─ third_party/
└─ docs/
```

### 7.2 依赖

| 依赖 | 版本 | 用途 | 许可 |
|---|---|---|---|
| Qt | 6.8 LTS（Widgets、Network） | UI、设置、文件库 | LGPL-3.0（动态链接） |
| FFmpeg | 7.x shared，LGPL 构建 | 封装、AAC、解码、软件 H.264 | LGPL-2.1+ |
| OpenH264 | 2.x | 软件 H.264 兜底 | BSD-2-Clause |
| NVENC SDK | 13.x | NVIDIA 硬件编码 | NVIDIA SDK 许可 |
| oneVPL | 2025.x | Intel QSV | MIT |
| AMF | 1.4.x | AMD 硬件编码 | MIT |
| spdlog | 1.14+ | 日志 | MIT |
| nlohmann/json | 3.11+ | 配置 | MIT |
| Catch2 | 3.x | 测试 | BSL-1.0 |

- 不使用 libx264/libx265（GPL），除非产品决定开源。
- FFmpeg 使用动态库，便于 LGPL 合规和替换。
- 依赖通过 vcpkg manifest 管理；NVENC/QSV/AMF 头文件放 `third_party/`。

### 7.3 构建

- 生成器：Phase 0 使用 Visual Studio 17 2022 生成器（本机 BuildTools 已验证）；后续可增加 Ninja Multi-Config 预设，便于生成 `compile_commands.json`。
- 预设：`windows-x64-debug`、`windows-x64-release`。
- C++20；MSVC `/W4 /permissive- /utf-8`。
- 运行库：Release 使用 `/MD`（Qt/FFmpeg 动态库）或 `/MT`（需统一）；第一版用 `/MD` 并随包 VC 运行库。
- 产物：`hmmob-ev.exe` + `hmmob_core.dll`（或静态库）+ Qt/FFmpeg 运行库 + `resources/`。

### 7.4 当前环境

本机已确认：

- Visual Studio BuildTools 2022，MSVC 14.44.35207。
- VS 自带 CMake 与 Ninja；MSBuild 与 `vcvars64.bat` 可用。
- Git 2.56；Python 3.14；winget/choco 可用。
- GPU：AMD Radeon RX 6750 GRE 12GB，AMF 运行时 `C:\Windows\System32\amfrt64.dll` 已存在。
- 磁盘剩余约 268GB。
- 尚未安装 Qt 6.8、vcpkg、FFmpeg 开发库、AMF 头文件。
- 本机没有 NVIDIA/Intel GPU，Phase 0 先验证 AMF；NVENC/QSV 在后续有对应硬件的机器上验证。

Phase 0 需要补齐这些依赖。

---

## 8. 分阶段计划

### Phase 0：环境与技术验证（3~5 天）

- 安装 Qt 6.8、vcpkg、FFmpeg 7 LGPL、编码 SDK 头文件。
- 最小程序：DXGI 抓屏 4K60 → NVENC 编码 → MP4。
- 验证 HDR 显示器抓取格式与色调映射。
- 验证 WASAPI Loopback + 麦克风混音。
- 输出：可行性报告、性能数据、依赖清单。

退出条件：4K60 抓屏+编码 5 分钟无异常；HDR 画面色调正确；音频可录。

### Phase 1：核心录制（4~6 周）

- 采集：DXGI 全屏/区域、WGC 窗口、Media Foundation 摄像头、WASAPI 音频。
- 合成：D3D11 场景、摄像头/水印/鼠标、HDR 色调映射、NV12 转换。
- 编码：NVENC/QSV/AMF + 软件兜底。
- 封装：MKV 临时 + MP4 remux、崩溃恢复、暂停/恢复。
- 输出：可用的命令行/最小 UI 录制内核。

退出条件：4K60 10 分钟录制通过；音画同步 < 50ms；硬件回退可用；崩溃恢复可用。

### Phase 2：产品化（3~4 周）

- Qt 主窗口、区域选择器、悬浮条、托盘、快捷键、设置、文件库。
- 多显示器/DPI/设备热插拔。
- 预览播放器。
- 安装包（Inno Setup）、日志、崩溃转储。
- 输出：可交付的 V1 桌面产品。

退出条件：全部 P0 功能通过手工测试；安装/卸载正常；常见场景无崩溃。

### Phase 3：后期能力（2~3 周）

- 首尾裁剪、片段合并。
- 标注/画笔、鼠标高亮。
- 摄像头布局模板、水印编辑。
- 输出：V1.5。

### Phase 4：RTMP（2 周）

- FLV/RTMP 输出、推流设置、断线重连、录制+推流并行。
- 输出：V1.6。

总计：核心 V1 约 2~3 个月；完整版约 4~5 个月（1 名有经验的 C++ 开发者）。

---

## 9. 测试与验收

### 9.1 单元测试

- 时间戳/时间基转换、暂停偏移。
- 有界队列、丢帧策略。
- 配置读写/版本迁移。
- HDR 色调映射数学（离线样例）。
- 文件命名、分段策略。

### 9.2 集成测试

- 合成测试图 → 编码 → 封装 → `ffprobe` 校验时长/分辨率/帧率/音轨。
- 4K60 10 分钟压力测试：丢帧数、CPU/GPU 占用、内存增长。
- 音频/视频同步：录制带节拍声/秒表的画面，测量偏差。
- 编码器切换：强制硬件失败，验证回退。
- 崩溃恢复：录制中 kill 进程，重启恢复。
- RTMP：本地 RTMP 服务器（如 SRS/nginx-rtmp）推流 30 分钟，验证断线重连。

### 9.3 手工验收矩阵

| 场景 | 检查点 |
|---|---|
| 单显示器 1080p | 全屏/区域/窗口/摄像头 |
| 单显示器 4K | 4K60、HDR 开/关 |
| 双显示器不同 DPI | 选区、坐标、预览 |
| 摄像头热插拔 | 自动恢复/提示 |
| 音频设备切换 | 系统声/麦克风 |
| 暂停/恢复 | 时间轴、音画同步 |
| 磁盘满/路径无权限 | 错误提示、文件完整性 |
| 硬件编码不可用 | 软件回退、UI 提示 |
| RTMP 断网 | 本地录制继续、重连 |

---

## 10. 风险与缓解

| 风险 | 影响 | 缓解 |
|---|---|---|
| 4K60 + 叠加层 GPU 过载 | 丢帧、卡顿 | 零拷贝、VideoProcessor、纹理池、性能预算与降级策略 |
| HDR 色调映射不准确 | 偏色/过曝 | 真机校准、可配置白点、提供 SDR 回退 |
| 编码器驱动兼容性 | 初始化失败/花屏 | 能力探测、多编码器回退、错误日志 |
| WGC 边框/受保护内容 | 无法录制/黄框 | 关闭边框、明确提示、DXGI 兜底 |
| Qt + D3D11 预览集成 | 黑屏/闪烁 | 原生子窗口 + SwapChain，不用 QWidget 绘制 |
| FFmpeg GPL 污染 | 法律风险 | 只用 LGPL 组件；禁用 libx264/libx265 |
| 杀软误报 | 用户无法安装 | 不用全局钩子；代码签名；提交误报 |
| 音频设备变化/漂移 | 音画不同步 | 设备通知、音频主时钟、漂移校正 |
| 崩溃导致文件损坏 | 用户投诉 | 临时 MKV + remux、启动恢复 |
| 开发周期超预期 | 延期 | 严格分阶段，Phase 0 先验证高风险项 |

---

## 11. 许可与合规

- Qt：LGPL-3.0，动态链接，提供替换库的方式；不静态链接闭源商用（除非购买商业许可）。
- FFmpeg：LGPL-2.1+，动态链接；构建时禁用 `--enable-gpl`、`--enable-nonfree`；不使用 libx264/libx265。
- OpenH264：BSD-2-Clause；注意 Cisco 专利许可条款。
- NVENC/QSV/AMF：遵循各自 SDK 许可；随包分发时保留版权声明。
- 不复制 EVCapture 的代码、图标、UI 资源、文案；本项目为 clean-room 实现。
- 默认不收集用户数据；如后续加入遥测，需显式同意。

---

## 12. 已确定的默认决策

| 决策 | 取值 |
|---|---|
| 产品名 | Hmmob EV |
| UI 框架 | Qt 6 Widgets |
| 图形 API | D3D11（不用 D3D12） |
| 进程模型 | 单进程 + `hmmob_core` 库；二期可拆独立录制进程 |
| 本地容器 | 临时 MKV → 停止时 remux MP4 |
| 推流容器 | FLV over RTMP |
| 软件编码 | OpenH264 / FFmpeg 原生 H.264，1080p 上限 |
| 支持系统 | Windows 10 1809+ / Windows 11 x64 |
| 安装包 | Inno Setup（Phase 2） |
| 自动更新 | 第一版不做 |
| 遥测 | 默认关闭 |
| 界面语言 | 中文优先，英文预留 |

---

## 13. 附录：EV 分析证据索引

| 证据 | 位置/内容 |
|---|---|
| Qt5 | `Qt5Core.dll`、`Qt5Widgets.dll`、`Qt5Network.dll` |
| FFmpeg 4.2 | `avcodec-58.dll`、`avformat-58.dll`、`avutil-56.dll`、`swscale-5.dll`、`swresample-3.dll` |
| DXGI 桌面复制 | `mcore.dll` 字符串 `DuplicateOutput`、`DuplicateOutput1`、`AcquireNextFrame`、`IDXGIOutput1` |
| D3D11 合成 | `mcore.dll` 导出 `GsDevice/GsScene/GsView/GsItem/GsTexture/GsFilter`；导入 `d3d11.dll`、`D3DCOMPILER_47.dll` |
| HDR | `mcore.dll` 字符串 `is_hdr`、`sdr_white_level`、`GsScreenDup` |
| WGC | `mc_winrt.dll` 字符串 `GraphicsCaptureItem`、`Direct3D11CaptureFramePool`、`GraphicsCaptureSession` |
| WASAPI | `EVCapture.exe` 导入 `Mmdevapi.dll`；字符串 `WASAPI`、`Loopback`、`Mic`、`GetDefaultAudioEndpoint` |
| DirectShow 摄像头 | `EVCapture.exe` 字符串 `DShow`、`Camera` |
| 硬件编码 | `evc_nvenc.dll`、`evc_qsv11.dll`、`evc_amf.dll`；`NV_ENC_*`、`MFX_*`、`AMFInit` |
| 封装/修复 | `FLAVLib.dll` 导出 `FLAV_start`、`FLAV_write_video_frame`、`FLAV_StartRepair`；导入 `avformat-58.dll` |
| 全局快捷键 | `win-hook.dll` 导出 `install_hook`、`add_key_hook_proc` |
| 本地流媒体 | `mediaserver.exe` 字符串 `rtmp`、`hls`、`flv`、`websocket` |
| 崩溃处理 | `crash_handler.exe`、`Mp4FixDll.dll`、`BugReport.exe` |
