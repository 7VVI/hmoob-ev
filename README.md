# Hmmob EV

Windows 高性能录屏软件。

当前阶段：Phase 0 完成（DXGI 抓屏 + AMD AMF 编码 + MP4 验证）；Phase 1 音频子项目完成并端到端验证。

## Phase 0 状态

Phase 0 核心管线（DXGI Desktop Duplication → D3D11 BGRA→NV12 → AMD AMF HEVC/H.264 → FFmpeg MP4）已在本机原生 **2560×1440@60** 下连续运行 10 分钟：输出 36,001 帧、59.99 fps、0 编码错误，MP4 全片解码无错误。完整结果见 [`docs/phase0-report.md`](docs/phase0-report.md)。

> 4K60 与真实 HDR 需要 4K/HDR 显示器及相应 GPU 才能验证，本机（2560×1440 SDR）未覆盖。

`evspike` 录制命令见下方「evspike 用法」。

## Phase 1 音频状态

WASAPI 系统声 Loopback + 麦克风采集、混音（增益/静音/软限幅）、AAC-LC 48 kHz 立体声编码与 MP4 音轨已实现并接入 `evspike`；`ctest` 86/86 通过（0 失败；无呈现桌面或无声输出时个别硬件用例跳过）。音视频同封装有自动化测试覆盖（`FFmpeg muxer writes HEVC+AAC MP4`）。

端到端已验证：10 秒录制输出 2560×1440@60 HEVC + AAC 48 kHz 立体声 MP4，全片解码退出码 0，麦克风音轨为真实内容；系统无播放时 loopback 输出数字静音。详见 [`docs/phase1-audio-report.md`](docs/phase1-audio-report.md)。

## 前置条件

- Windows 10/11 x64
- Visual Studio 2022，安装「使用 C++ 的桌面开发」工作负载
- CMake >= 3.29
- Git
- AMF 编码头文件：首次构建前运行 `powershell -ExecutionPolicy Bypass -File .\scripts\fetch_amf.ps1`（固定 AMF v1.5.3，需要网络）

## 构建

通用写法（CMake 已在 `PATH` 中）：

```powershell
cmake --preset windows-x64-release
cmake --build --preset windows-x64-release
.\build\release\Release\evspike.exe
```

> 注意：下面的完整路径仅适用于本机的 Visual Studio 2022 BuildTools 安装位置。其他机器请使用上面的通用写法，或替换为本机实际的 `cmake.exe` 路径。

```powershell
$cmake = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
& $cmake --preset windows-x64-release
& $cmake --build --preset windows-x64-release
.\build\release\Release\evspike.exe
```

## 图形界面

`hmmob_ev_app` 是无边框录制窗口（Win11 圆角、自绘标题栏，可拖动/缩放/最大化）：左侧为实时预览（640×360@30），录制开始前即可看到桌面画面；录制期间预览暂停（录制管线独占抓屏路径），停止并保存完成后自动恢复。右侧为三张设置卡片：

- 录制内容：全屏（可用）/ 区域 / 窗口 / 摄像头（后三者暂为占位提示）
- 音频设置：麦克风、系统声音的默认设备与电平条，喇叭图标切换静音
- 视频设置：分辨率、帧率、编码格式；高级设置内可调码率、HDR 与多显示器

底部为保存路径、预计时长/大小与开始/停止按钮。顶部导航的窗口/区域/摄像头录制、设置、打开文件同样为后续版本占位。

> 预览由 GPU 缩放到 640×360 后读回 CPU 绘制，用于确认画面与构图，不是最终的低延迟预览；录制画质与预览无关。停止后保存完成前请勿关闭进程。

```powershell
.\build\release\Release\hmmob_ev_app.exe
```

自动化验证可使用 `--smoke <秒数> <输出路径>`：窗口显示后自动按 60 fps / HEVC / 音频开启开始录制，到时停止，并把统计写入 `<输出路径>.smoke.txt` 后退出（成功退出码 0，录制失败 1，参数错误 2）。

```powershell
.\build\release\Release\hmmob_ev_app.exe --smoke 5 C:\temp\ui-smoke.mp4
```

## evspike 用法

`evspike` 抓取指定显示器的桌面，转换为 NV12，用 AMD AMF 编码，并封装为 MP4。默认同时录制系统声音（WASAPI loopback）和麦克风，混音后编码为 AAC 音轨；桌面无新帧时重复上一帧，按固定帧率输出。

```powershell
.\build\release\Release\evspike.exe --output C:\temp\out.mp4 --seconds 10 --fps 60 --codec hevc --bitrate 50000000
```

只录系统声音（关闭麦克风）：

```powershell
.\build\release\Release\evspike.exe --output C:\temp\sys.mp4 --seconds 10 --no-mic --system-volume 1.5
```

不录音频（仅视频）：

```powershell
.\build\release\Release\evspike.exe --output C:\temp\video.mp4 --seconds 10 --no-audio
```

| 参数 | 说明 | 默认值 |
| --- | --- | --- |
| `--output <path>` | 输出 MP4 路径 | `out.mp4` |
| `--seconds <n>` | 录制时长（秒），必须 > 0 | `10` |
| `--fps <n>` | 目标帧率，范围 1..1000 | `60` |
| `--codec <name>` | `hevc` 或 `h264` | `hevc` |
| `--display <n>` | 显示器索引，必须 >= 0 | `0` |
| `--bitrate <bps>` | 目标码率（bit/s），必须 > 0 | `50000000` |
| `--hdr` | 请求 HDR 抓屏并色调映射为 SDR；显示器为 SDR 时打印降级警告并按 SDR 录制 | 关闭 |
| `--no-audio` | 关闭全部音频（仅录视频） | 音频开启 |
| `--no-system-audio` | 关闭系统声音（loopback） | 系统声音开启 |
| `--no-mic` | 关闭麦克风 | 麦克风开启 |
| `--system-volume <x>` | 系统声音增益，范围 0..4 | `1.0` |
| `--mic-volume <x>` | 麦克风增益，范围 0..4 | `1.0` |
| `-h`, `--help` | 显示帮助并退出 | — |

录制结束后输出统计：`acquired`（成功 `Acquire` 返回的帧数，包含新桌面帧和仅指针更新帧；`Acquire` 超时复用上一帧不计入）、`new_desktop`（真正的新桌面帧数）、`encoded`（实际编码帧数）、`packets`、`bytes`、`encode_errors`、`audio_packets`、`audio_bytes`、`elapsed`、`avg_fps`。例如 10 分钟静态桌面录制为 `acquired=171 new_desktop=171 encoded=36001`，其余编码帧来自超时后重复上一帧。

音频说明：系统声音或麦克风不可用时会打印 `warning` 并降级为可用的一路；两路都不可用（或 AAC 编码器初始化失败）时自动改为仅录制视频，不会伪造静音音轨。相同 `WasapiCapture` 在混音器仍持有其环缓冲时不会被重新初始化。

退出码：`0` 表示成功，`1` 表示失败（参数错误、初始化失败、采集/编码/封装失败等）。

> 4K60 录制需要 4K 显示器与支持 4K 实时编码的 GPU；在 2560x1440 屏幕上只能按原生分辨率验证。
