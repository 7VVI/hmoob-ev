# Hmmob EV

Windows 高性能录屏软件。

当前阶段：Phase 0 — DXGI 4K60 抓屏 + AMD AMF 编码 + MP4 验证。

## Phase 0 状态

Phase 0 核心管线（DXGI Desktop Duplication → D3D11 BGRA→NV12 → AMD AMF HEVC/H.264 → FFmpeg MP4）已在本机原生 **2560×1440@60** 下连续运行 10 分钟：输出 36,001 帧、59.99 fps、0 编码错误，MP4 全片解码无错误。完整结果见 [`docs/phase0-report.md`](docs/phase0-report.md)。

> 4K60 与真实 HDR 需要 4K/HDR 显示器及相应 GPU 才能验证，本机（2560×1440 SDR）未覆盖。

`evspike` 录制命令见下方「evspike 用法」。

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

## evspike 用法

`evspike` 抓取指定显示器的桌面，转换为 NV12，用 AMD AMF 编码，并封装为 MP4。桌面无新帧时重复上一帧，按固定帧率输出。

```powershell
.\build\release\Release\evspike.exe --output C:\temp\out.mp4 --seconds 10 --fps 60 --codec hevc --bitrate 50000000
```

| 参数 | 说明 | 默认值 |
| --- | --- | --- |
| `--output <path>` | 输出 MP4 路径 | `out.mp4` |
| `--seconds <n>` | 录制时长（秒），必须 > 0 | `10` |
| `--fps <n>` | 目标帧率，范围 1..1000 | `60` |
| `--codec <name>` | `hevc` 或 `h264` | `hevc` |
| `--display <n>` | 显示器索引，必须 >= 0 | `0` |
| `--bitrate <bps>` | 目标码率（bit/s），必须 > 0 | `50000000` |
| `--hdr` | 请求 HDR 抓屏（色调映射在 Task 10 实现，当前会被拒绝） | 关闭 |
| `-h`, `--help` | 显示帮助并退出 | — |

录制结束后输出统计：`acquired`（成功取到的帧数，含重复帧）、`new_desktop`（真正的新桌面帧数）、`encoded`、`packets`、`bytes`、`encode_errors`、`elapsed`、`avg_fps`。运行失败时退出码为 1；请求 HDR 且显示器为 HDR 时退出码为 2（Task 10 占位）。

> 4K60 录制需要 4K 显示器与支持 4K 实时编码的 GPU；在 2560x1440 屏幕上只能按原生分辨率验证。
