# 容器格式、音频参数与设备枚举实施计划

**Goal:** 支持 mp4/mov/mkv/avi/flv/wmv 六种容器，音频可选 AAC/PCM、44.1/48 kHz、96~320 kbps；首页麦克风/系统声下拉枚举真实设备。

**Constraints:**
- FLV 仅 H.264；HEVC 仅 mp4/mov/mkv；AVI/ASF 仅 H.264；PCM 不支持 FLV。
- 音频编码器可用：AAC（FFmpeg 原生）、PCM s16le；无 MP3。
- 画质即码率，不新增画质档位。

---

## Task F: 容器与音频参数（引擎）

**Files:** `src/core/mux/FfmpegMuxer.*`、新增 `src/core/audio/PcmEncoder.*`/`AudioEncoder.h`、`src/core/recorder/RecorderConfig.h`、`Recorder.cpp`、`src/spike/main.cpp`、测试。

- `RecorderConfig` 增加：`container`（由扩展名决定，显式枚举）、`audio_format`（aac/pcm_s16le）、`audio_sample_rate`（44100/48000）、`audio_bitrate`。
- `FfmpegMuxer::Open` 用 `avformat_alloc_output_context2` 按扩展名/容器选择 muxer；校验组合：
  - flv：codec 必须 h264，audio 不能 pcm；
  - avi/asf：codec 必须 h264；
  - hevc + flv/avi/asf → 明确错误；
  - mp4/mov 设置 `movflags +faststart`，其余不加。
- 视频：保留 avcC/hvcC + 4 字节长度前缀；容器 codec_tag 按容器设置（mp4/mov hvc1/avc1；其他默认）。
- 音频：新增 `AudioEncoder` 抽象（AAC / PCM）。PCM 直接输出 s16le 交织包（1024 帧/包），PTS 由累计采样数计算；AAC 沿用现有编码器但采样率/码率来自配置。
- Recorder 使用配置的采样率/码率/格式；WasapiCapture、AudioMixer、音频预算全部按采样率参数化（去掉 48000 常量）。
- evspike 增加 `--container <ext>`、`--audio-format aac|pcm`、`--sample-rate <n>`、`--audio-bitrate <n>` 便于验证。

验收：六种容器各录 3 秒（H.264/AAC 为主，mp4/mov/mkv 另测 HEVC；flv 测 AAC；avi/wmv 测 PCM），`ffmpeg -i` 识别正确、`-f null -` 解码 0；不兼容组合返回清晰错误。

---

## Task G: WASAPI 设备枚举（引擎）

**Files:** `src/core/audio/WasapiCapture.*`、`RecorderConfig.h`、`Recorder.cpp`、`evspike`、测试。

- 新增 `static std::vector<AudioDeviceInfo> EnumerateDevices(bool loopback)`（id UTF-8、name UTF-8、是否默认）。
- `WasapiCapture::Initialize(..., const std::string& device_id)`：空串=默认设备；否则 `GetDevice(wide(device_id))`；设备不存在时返回明确错误。
- `RecorderConfig` 增加 `system_device_id`、`mic_device_id`；Recorder 传入；失败时沿用现有降级/警告逻辑。
- evspike 增加 `--list-audio-devices`、`--system-device <id>`、`--mic-device <id>`。

验收：列出本机麦克风与扬声器端点；按 ID 录制成功；拔出/无效 ID 时该路降级且视频继续。

---

## Task H: 界面接线

- 设置弹窗新增：
  - 视频：保存格式（六种）、现有码率；
  - 音频：格式（AAC/PCM）、采样率（44.1/48 kHz）、码率（96/128/192/320 kbps）；
  - 组合不兼容时禁用 / 保存时提示。
- 首页音频设置两行改为真实设备下拉（默认设备 + 枚举项），选择写入 RecorderConfig。
- 保存路径随容器扩展名联动：自动路径跟随格式；用户手改路径时不强制。
- 高级设置不再存在，所有新选项都在「设置」。

验收：各格式录制可解码；切换格式后输出扩展名正确；设备下拉显示真实名称并生效。
