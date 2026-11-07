# Phase 0 验证报告

## 1. 结论

Phase 0 核心管线在 **2560×1440@60** 下连续运行 10 分钟，未出现崩溃、设备丢失或编码错误：

- 10 分钟录制共输出 **36,001 帧**，实际 **59.99 fps**，`encode_errors=0`。
- 输出 MP4 时长 **00:10:00.02**，全片解码（`ffmpeg -f null -`）退出码 0、无错误输出。
- 4K60 与真实 HDR 采集因本机显示器为 2560×1440 SDR **未验证**，详见第 6 节。

结论：**Phase 0 通过**（原生 1440p60 范围）。

> 范围说明：实施计划对设计规格中的 Phase 0 验收标准做了重新界定。设计规格原始标准为：4K60 连续录制 5 分钟、真实 HDR 色彩、可录制音频。本次验证中，4K60 因本机为 2560×1440 SDR 而未验证；真实 HDR 仅有合成测试，未验证真实 HDR 显示器；音频已由计划明确推迟到 Phase 1。因此本报告的“通过”仅针对**原生 1440p60 的采集→编码→封装**范围，原始验收标准中的 4K60、真实 HDR 与音频仍为未完成项。

## 2. 环境

| 项目 | 值 |
| --- | --- |
| 日期 | 2026-11-07 |
| 操作系统 | Windows 11 Pro 10.0.26300 |
| GPU | AMD Radeon RX 6750 GRE 12GB |
| 驱动版本 | 32.0.21045.16010 |
| 显示器 / 分辨率 / HDR | 单显示器，2560×1440，SDR |
| 编码器 | AMD AMF HEVC Main，`hvc1`，NV12，BT.709，tv（studio）range |
| 编译器 | MSVC 14.44.35207（Visual Studio 2022 BuildTools） |
| 构建系统 | CMake ≥ 3.29，`windows-x64-release` preset |
| 第三方依赖 | vcpkg FFmpeg 9.0.2、Catch2 3、AMF 1.5.3 |

录制命令：

```powershell
.\build\release\Release\evspike.exe --output C:/temp/hmmob-10min.mp4 --seconds 600 --fps 60 --codec hevc --bitrate 50000000
```

运行时首行输出：

```text
display=0 2560x1440 format=87 fps=60 codec=hevc bitrate=50000000
```

`format=87` 为 `DXGI_FORMAT_B8G8R8A8_UNORM`。

## 3. 管线

```text
DXGI Desktop Duplication
        │  BGRA8（新帧或重复上一帧）
        ▼
[可选] HDR 色调映射（ACES，FP16 scRGB / R10G10B10A2 PQ → BGRA8）
        ▼
D3D11 VideoProcessor（GPU BGRA → NV12）
        ▼
AMF 自有 NV12 表面（GPU CopyResource，无 CPU 回读）
        ▼
AMD AMF HEVC / H.264（低延迟、CBR、固定 GOP）
        ▼
FFmpeg MP4 封装（hvcC / avcC extradata，固定 PTS）
```

各部分职责：

- **采集**：`DxgiCapture` 通过 Desktop Duplication 获取桌面纹理；桌面无新帧时沿用上一帧。采集失败（设备移除、访问丢失、模式变化）会向上层返回错误并以非零退出码结束，而不是静默产出坏文件。
- **转换**：`VideoProcessor` 使用 D3D11 VideoProcessor 在 GPU 上完成 BGRA→NV12，输出纹理直接交给编码器。
- **编码**：`AmfEncoder` 将转换结果 `CopyResource` 到 AMF 自有表面（GPU 内复制，避免 CPU 回读），以低延迟 CBR 模式编码；编码器回调产出 Annex-B 访问单元。
- **封装**：`FfmpegMuxer` 将参数集转换为 `hvcC`/`avcC` extradata，按固定帧率写入 PTS，封装为 MP4。
- **节奏控制**：`QpcClock` 按绝对截止时间调度，目标帧间隔为 `1/fps`；短暂停顿后不会连发两帧。桌面无更新时重复上一帧，保证输出恒定帧率与时长。

## 4. 10 分钟结果

进程统计（正常退出，exit 0）：

```text
acquired=171 new_desktop=171 encoded=36001 packets=36001 bytes=221966550 encode_errors=0 elapsed=600.12s avg_fps=59.99
```

| 指标 | 值 |
| --- | --- |
| 目标时长 / 实际耗时 | 600 s / 600.12 s |
| 输出帧数 | 36,001 |
| 平均帧率 | 59.99 fps |
| 新桌面帧 | 171（静态桌面，其余为重复上一帧） |
| 编码错误 | 0 |
| 编码包 / 包字节数 | 36,001 / 221,966,550 B |
| 输出文件 | `C:\temp\hmmob-10min.mp4` |
| 文件大小 | 222,277,944 B（约 212 MiB） |

FFmpeg 元数据：

| 项目 | 值 |
| --- | --- |
| 时长 | 00:10:00.02 |
| 平均码率 | 2,963 kb/s |
| 视频编码 | `hevc (Main) (hvc1)` |
| 像素格式 / 色彩 | `yuv420p(tv, bt709)` |
| 分辨率 | 2560×1440 |
| 帧率 | 60 fps / 60 tbr |
| `mp4_data_incomplete` | false |

全片解码校验：

```powershell
ffmpeg -v error -i C:/temp/hmmob-10min.mp4 -f null -
```

退出码 0，无任何错误输出，共解码 36,001 帧。容器完整，文件可被标准解码器完整读取。

> 说明：本次录制的内容为静态桌面，实际码率约 2.96 Mbps，明显低于 50 Mbps 的目标码率。这是 CBR 编码器在低运动内容下的正常表现，不代表管线吞吐上限；码率目标需用高运动内容复测。

产物来源说明：本次 10 分钟产物由最终 HDR-only 提交 `7483f3d`（`fix: apply bt2020 matrix only to pq`）之前的 SDR 构建生成。`7483f3d` 仅调整 HDR PQ 的 BT.2020 矩阵与色调曲线路径，SDR 路径未改动；当前二进制已用 3 秒与 2 秒短录制重新验证，SDR 采集→编码→封装行为一致。

## 5. 测试覆盖

| 套件 | 结果 |
| --- | --- |
| 完整 ctest（含 GPU/AMF/DXGI 集成测试） | **51/51 通过** |
| 无 GPU 环境（排除 `[integration]`） | **31/31 通过** |

组件覆盖：

- **QPC 时钟**：单调递增、大 tick 计数无溢出、重复调用不回退。
- **FrameQueue**：FIFO 顺序、容量拒绝、超时、关闭唤醒与排空。
- **D3D11 设备**：设备创建、显示器枚举、`IsHdrColorSpace` 与 `FromDesc1` 映射。
- **DXGI 采集**：启动/重置、非法显示器索引报错、主显示器实际取帧。
- **BGRA→NV12**：灰、白、黑、红四色转换正确性，尺寸不匹配、重复初始化与失败后重置。
- **AMF 编码**：HEVC 与 H.264 各编码 30 帧，验证输出包数量、关键帧与字节流。
- **MP4 封装**：HEVC/H.264 可解码 MP4、非法参数、写入失败锁存、Close 前写入报错、合成访问单元。
- **extradata**：`avcC`/`hvcC` 构造、三字节起始码、缺失参数集、截断 SPS、High profile 扩展、非 Main HEVC 拒绝、Annex-B 长度前缀转换。
- **HDR 色调映射（合成）**：FP16 scRGB → BGRA8、scRGB 饱和红保持 BT.709、PQ R10G10B10A2 → BGRA8、PQ 饱和红经 BT.2020 矩阵、非法格式/尺寸/未初始化拒绝、重复初始化。

## 6. 限制与未验证项

1. **4K60 未验证**：本机显示器为 2560×1440，无法产生 4K 桌面；4K60 的采集、转换与编码吞吐需在 4K 显示器机器上复测。
2. **真实 HDR 未验证**：本机为 SDR 显示器，仅通过合成的 FP16 scRGB / R10G10B10A2 PQ 测试验证了色调映射算法，未验证真实 HDR10/scRGB 桌面采集链路。
3. **码率未按目标验证**：静态桌面内容下实际码率约 2.96 Mbps，未验证 50 Mbps 目标；需用高运动内容复测。
4. **`ACCESS_LOST` 当前是致命错误**：UAC 安全桌面、锁屏、显示器模式切换等都会导致录制以退出码 1 中止，尽管 `DxgiCapture` 具备延迟重建 duplication 的能力。Phase 1 应实现重试与恢复。
5. **缺少存活/停滞检测**：显示器休眠/DPMS 或驱动短暂异常若未产生 `ACCESS_LOST`，管线可能用重复上一帧填满剩余时长并以退出码 0 结束；`new_desktop` 是唯一可观测信号。Phase 1 应对长时间无新桌面帧发出警告或失败。
6. **非 ASCII 输出路径失败**：`main` 使用 ANSI `argv`，而 FFmpeg 将路径按 UTF-8 解释，中文等非 ASCII 输出路径会在 `avio_open` 返回 `EINVAL`。Phase 1 需要 `wmain` 或显式 UTF-8 转换。
7. **单纹理采集路径**：当前每帧复用单个转换输出纹理，编码期间无法并行准备下一帧；Phase 1 将引入纹理池。
8. **4K GPU 带宽未实测**：除 NV12→AMF 表面复制（4K60 约 746 MB/s）外，采集侧还有 BGRA8 `CopyResource`（3840×2160×4×60 ≈ 1.99 GB/s）；HDR 路径还包含 FP16 读取、BGRA8 写入与 BLT。4K60 余量需实测。
9. **缺少 CPU/GPU 占用与丢帧/延迟数据**：本次仅记录了 59.99 fps 的稳定性，没有采集利用率与端到端延迟，不能据此推断 4K60 或叠加层场景的性能余量。
10. **容器缺少色彩元数据**：MP4 中没有 `colr` box，播放器依赖 AMF 写入 SPS VUI 的 `tv, bt709` 信息。Phase 1 应设置 `AVCodecParameters` 的 color primaries/transfer/matrix/range 字段。
11. **HDR 白点校准待定**：PQ 路径在 203 nits 时白点约 232/255，scRGB 路径标称 SDR 白约 193/255，两者不一致；真实 HDR 显示器上的白点校准尚未进行。
12. **`VideoProcessor` 内容描述硬编码 60/1**：当前 Blt 路径无影响，但未来做帧率转换时需要按实际输入/输出帧率设置。
13. **Phase 0 为 CLI spike**：没有 Qt UI、音频、摄像头、叠加层或 RTMP 推流；这些属于 Phase 1 及后续计划。

## 7. 结论与下一步

Phase 0 目标达成：DXGI Desktop Duplication → D3D11 BGRA→NV12 → AMD AMF HEVC/H.264 → FFmpeg MP4 的核心管线在原生 2560×1440@60 下稳定运行 10 分钟，输出文件完整可解码，测试套件全部通过。

Phase 1 计划：

- 采集访问丢失恢复：`ACCESS_LOST` 后重建 duplication 并继续录制，而不是直接退出。
- 停滞检测：长时间无新桌面帧时告警或失败，避免静默输出重复帧。
- Unicode 输出路径：改用 `wmain` 或显式 UTF-8 转换，支持中文等非 ASCII 路径。
- 采集/转换纹理池，消除单纹理串行瓶颈，并实测 4K60 的采集与编码 GPU 带宽余量。
- 真实 HDR 验证：在 HDR10/scRGB 显示器上校准白点，并写入容器 `colr` 色彩元数据。
- 音频采集（WASAPI）与音视频同步。
- 摄像头采集与输入。
- D3D11 合成叠加层（摄像头画面、光标、水印等）。
- Qt 用户界面。
- 多显示器与热插拔处理。
- 自动化端到端冒烟测试（启动→短录制→ffprobe/解码校验→退出码断言）。
