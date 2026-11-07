# Phase 1 音频验证报告

## 1. 结论

Phase 1 音频子项目（WASAPI 系统声 Loopback + 麦克风 → 混音 → AAC-LC → MP4 音轨，接入 `evspike`）的编码实现与组件测试已完成：

- 音频链路各组件（环形缓冲、混音器、AAC 编码器、WASAPI 采集）与「视频 + 音频」MP4 封装均有自动化测试覆盖，完整 `ctest` **81/81 通过**（2 个因环境跳过）。
- `FFmpeg muxer writes HEVC+AAC MP4` 验证了 HEVC 视频 + AAC 音频写入同一 MP4、解复用后 AAC 解码成功、采样率/声道数/帧数/时长正确。
- **端到端带音频录制（真实设备录制 → 带音轨 MP4）在本次会话未验证**：当前会话没有正在呈现的物理显示器，DXGI 采集到 0 帧，管线无法写出任何视频包，音轨也随之无输出。该限制与音频改动无关，使用 `--no-audio` 可复现同样的 0 帧结果。详见第 6 节。

结论：**音频子项目代码完成、组件级验证通过；端到端验收待有活跃显示器的会话补测**。

## 2. 环境

| 项目 | 值 |
| --- | --- |
| 日期 | 2026-11-07 / 2026-11-08 |
| 操作系统 | Windows 11 Pro 10.0.26300 |
| GPU | AMD Radeon RX 6750 GRE 12GB |
| 显示器 | 2560×1440 SDR（本次验证会话中未呈现，见第 6 节） |
| 音频接口 | WASAPI 共享模式（loopback + 麦克风） |
| 音频编码 | AAC-LC，48 kHz，立体声，192 kbps，1024 采样/帧 |
| 分支 / 提交 | `phase0`，`c3f22c6`..`aa80a12` |
| 编译器 | MSVC 14.44.35207（Visual Studio 2022 BuildTools） |
| 第三方依赖 | vcpkg FFmpeg 9.0.2、Catch2 3、AMF 1.5.3 |

## 3. 管线

```text
WASAPI loopback（系统声） ──┐
                            ├─→ PcmRingBuffer（float32 交织，1 s）─→ AudioMixer（增益/静音/tanh 软限幅）
WASAPI 麦克风 ─────────────┘                                                  │
                                                                              ▼
                                              AacEncoder（AAC-LC 48k 立体声 192k）─→ AacPacket（PTS 由采样数计算）
                                                                              │
DXGI → VideoProcessor → AMF → AmfPacket ───────────────────────────────────────┤
                                                                              ▼
                                                     FFmpeg MP4（视频流 + 音频流，首视频包写入 header）
```

要点：

- **采集**：两路 `WasapiCapture` 各自在线程内以事件驱动方式读取，格式转换为 48 kHz 立体声 float32 写入环形缓冲；设备不支持事件回调（`IAudioClient::Initialize(EVENTCALLBACK)` 失败）时重新激活客户端退化为轮询，采集线程启用 MMCSS。
- **混音**：`AudioMixer` 对每路应用增益与开关，求和后以 `tanh` 软限幅，避免硬削波；某一路缺失或声道数不符时按静音处理。
- **编码**：`AacEncoder` 将任意长度 PCM 切片缓冲为 1024 采样帧，输出带采样数 PTS 与 `duration` 的 `AacPacket`；首包 PTS 为 `-1024`（AAC 编码器延迟），由 mov muxer 生成 edit list。
- **同步**：每编码一帧视频，按 `target_samples = (encoded_frames + 1) * 48000 / fps` 补齐音频采样，使音轨长度精确跟随视频帧数；录制结束时先 flush 视频编码器、再 flush AAC 编码器。
- **启动对齐**：两路采集在麦克风、AAC 编码器和 muxer 初始化期间已开始写环形缓冲，进入录制循环前会清空该预滚数据，避免音频整体滞后视频、以及系统声与麦克风之间产生固定偏移。
- **封装**：`FfmpegMuxer` 以 AAC `AudioSpecificConfig`（5 字节 `11 90 56 E5 00`）作为 `extradata`，在首个视频包到来时写 MP4 header；header 之前的音频包有上限缓冲，超限即报错而不是静默丢弃。

## 4. 实现与参数

`evspike` 默认同时录制系统声与麦克风，全部音量/开关可通过命令行控制：

| 参数 | 说明 | 默认值 |
| --- | --- | --- |
| `--no-audio` | 关闭全部音频（仅录视频） | 音频开启 |
| `--no-system-audio` | 关闭系统声音（loopback） | 系统声音开启 |
| `--no-mic` | 关闭麦克风 | 麦克风开启 |
| `--system-volume <x>` | 系统声音增益，范围 0..4 | `1.0` |
| `--mic-volume <x>` | 麦克风增益，范围 0..4 | `1.0` |

降级策略（均有实现，`README.md` 同步说明）：

1. 单路设备不可用：打印 `warning: system audio unavailable (0x...)` 或 `warning: microphone unavailable (0x...)`，用可用的一路继续录制。
2. 两路都不可用，或 AAC 编码器初始化失败：打印警告并改为仅录视频，不伪造静音音轨。
3. 录制过程中某路采集线程失败：停掉该路并断开混音器输入；两路都失败时关闭音频，视频继续。
4. 音频编码或封装失败：整体失败并以退出码 1 结束，不产出半截文件后继续运行。

统计行新增 `audio_packets`、`audio_bytes`：

```text
acquired=... encoded=... packets=... bytes=... encode_errors=0 audio_packets=... audio_bytes=... elapsed=...s avg_fps=...
```

## 5. 验证

### 5.1 测试套件

```powershell
$ctest = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe"
& $ctest --test-dir build/release -C Release --output-on-failure
```

结果：**81/81 通过**，其中 2 个跳过（Catch2 `SKIP`）：

| 跳过用例 | 原因 |
| --- | --- |
| `DXGI capture acquires frames from the primary display` | 当前会话桌面未呈现（无新桌面帧） |
| `WasapiCapture loopback receives packets` | 端点空闲，录制期间无音频播放 |

音频相关覆盖：

| 套件 | 用例数 | 覆盖内容 |
| --- | --- | --- |
| `PcmRingBuffer` | 6 | 交织写入/读取、正好写满不丢帧、溢出丢最旧、多次回绕、欠载返回可用帧数、非法参数拒绝 |
| `AudioMixer` | 6 | 双路增益求和、缺失源按静音、静音但排空、声道数不符时忽略该路、软限幅不硬削波、非法参数拒绝 |
| `AacEncoder` | 6 | 1 秒立体声正弦、非固定切片下 PTS/时长稳定、输出可解码且双声道完好、Flush 后拒绝 Encode、非法配置拒绝、48 kHz 立体声 ASC 字节正确 |
| `WasapiCapture` | 4 | loopback 收包、麦克风收包、初始状态干净、非法参数拒绝 |
| `FfmpegMuxer`（音频） | 6 | 视频 header 前音频缓冲与上限、音频关闭时拒绝音频包、缺 `extradata` 拒绝、仅音频时 Close 报错、写入失败锁存 |

### 5.2 音视频同封装证据

`FFmpeg muxer writes HEVC+AAC MP4`（60 帧 HEVC + 1 秒 48 kHz 立体声）：

- 解复用后 `nb_streams == 2`，视频 `AV_CODEC_ID_HEVC`、音频 `AV_CODEC_ID_AAC`。
- 音频流 `sample_rate == 48000`、`ch_layout.nb_channels == 2`、`frame_size == 1024`、`initial_padding == 1024`、`extradata_size` 与 AAC 编码器输出一致。
- 逐包断言 `pts == dts` 且 `duration` 恒定；AAC 解码成功，解码帧采样率/声道数正确。

### 5.3 端到端尝试（未通过，环境原因）

命令：

```powershell
.\build\release\Release\evspike.exe --output C:/temp/hmmob-audio.mp4 --seconds 3 --fps 60 --codec hevc --bitrate 50000000
```

输出：

```text
warning: achieved 0/180 target frames; output duration is shorter than requested
FfmpegMuxer: closing without a written header
muxer close failed
display=0 2560x1440 format=87 fps=60 codec=hevc bitrate=50000000
acquired=0 new_desktop=0 encoded=0 packets=0 bytes=0 encode_errors=0 audio_packets=0 audio_bytes=0 elapsed=3.01s avg_fps=0.00
```

（上面为含 `aa80a12` 启动对齐修复后的运行输出。）

`C:/temp/hmmob-audio.mp4` 为 0 字节（`moov atom not found`）。相同命令加 `--no-audio` 得到完全相同的 0 帧结果，说明失败发生在视频采集环节，与音频改动无关。

两路音频设备本身初始化正常（无 `warning` 输出），AAC 编码器也正常创建；由于 0 帧视频导致 header 从未写入，音轨没有落盘。这是本会话的显示器环境限制，不是音频链路缺陷，但也不构成音频端到端通过的证据。

## 6. 限制与未验证项

1. **端到端带音频 MP4 未验证**：需要有正在呈现的物理显示器时重跑 3 秒与 10 秒录制，并用 `ffmpeg -i` 检查 `hevc + aac`、48 kHz 立体声与解码退出码。本报告不声称该项已通过。
2. **音频不作为主时钟**：音轨长度由视频帧数推导（`(encoded_frames + 1) * 48000 / fps`）。若采集线程长时间阻塞导致实际采样不足，会用当前可读采样补齐（短时欠载），不做基于音频时钟的丢帧/补帧。Phase 1.5 处理。
3. **设备热切换未做**：录制开始后新增/移除设备不会重建 `WasapiCapture`；设备被移除时该路采集失败并按降级策略处理。
4. **无噪声抑制/回声消除/自动增益**：麦克风仅做采样格式转换与增益，未接入 AEC/NS/AGC。
5. **Loopback 在无声时输出静音**：WASAPI loopback 在系统不播放音频时不产生数据，混音后为静音轨；这与真实静音无法区分，属预期行为。
6. **声道布局固定**：混音与 AAC 输出固定 48 kHz 立体声；非立体声设备经 `swresample` 转换，未实现 5.1/7.1 透传。
7. **锁屏/安全桌面**：与 Phase 0 相同，`ACCESS_LOST` 目前仍是致命错误，音频链路无独立恢复能力。
8. **非 ASCII 输出路径**：仍受 Phase 0 限制（ANSI `argv` + FFmpeg UTF-8 路径），中文路径会失败。

## 7. 下一步（Phase 1.5）

- 在活跃显示器会话中完成 3 s / 10 s 端到端音频录制与解码校验，补入本报告。
- 音频主时钟：以采样数为准做视频帧节流或音频丢补，避免长时间运行后的音画漂移。
- 设备热切换与设备变化通知（`IMMNotificationClient`）。
- 麦克风 AEC/NS/AGC，以及可选的多声道直通。
- `ACCESS_LOST` 恢复与采集停滞检测（延续 Phase 0 未完成项）。
