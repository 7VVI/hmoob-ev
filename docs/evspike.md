# evspike 命令行工具

`evspike` 是开发调试用的命令行录制工具，与图形界面共用同一录制引擎。它抓取指定显示器或区域，转换为 NV12，用 AMD AMF 编码，并封装为所选容器；默认同时录制系统声音（WASAPI loopback）和麦克风。

```powershell
.\build\release\Release\evspike.exe --output C:\temp\out.mp4 --seconds 10 --fps 60 --codec hevc --bitrate 50000000
```

只录系统声音：

```powershell
.\build\release\Release\evspike.exe --output C:\temp\sys.mp4 --seconds 10 --no-mic --system-volume 1.5
```

仅视频：

```powershell
.\build\release\Release\evspike.exe --output C:\temp\video.mp4 --seconds 10 --no-audio
```

区域录制：

```powershell
.\build\release\Release\evspike.exe --output C:\temp\region.mp4 --region 100,100,320,180 --seconds 5
```

| 参数 | 说明 | 默认值 |
| --- | --- | --- |
| `--output <path>` | 输出路径，扩展名决定容器（mp4/mov/mkv/avi/flv/wmv） | `out.mp4` |
| `--seconds <n>` | 录制时长（秒），> 0 | `10` |
| `--fps <n>` | 目标帧率，1..1000 | `60` |
| `--codec <name>` | `hevc` 或 `h264` | `hevc` |
| `--display <n>` | 显示器索引，>= 0 | `0` |
| `--region x,y,w,h` | 区域录制（显示本地像素） | 关闭 |
| `--window <n>` | 按序号录制可见顶层窗口（1-based） | 关闭 |
| `--camera <n>` | 摄像头索引 | 关闭 |
| `--list-cameras` | 列出摄像头并退出 | — |
| `--list-audio-devices` | 列出麦克风与扬声器端点并退出 | — |
| `--system-device <id\|index>` | 系统声音设备 | 默认设备 |
| `--mic-device <id\|index>` | 麦克风设备 | 默认设备 |
| `--bitrate <bps>` | 视频码率 | `50000000` |
| `--audio-bitrate <bps>` | 音频码率（AAC） | `192000` |
| `--audio-format aac\|pcm` | 音频编码 | `aac` |
| `--sample-rate <n>` | 音频采样率（44100/48000） | `48000` |
| `--hdr` | 请求 HDR 抓屏并色调映射为 SDR | 关闭 |
| `--no-audio` / `--no-system-audio` / `--no-mic` | 关闭音频或单路 | 开启 |
| `--system-volume <x>` / `--mic-volume <x>` | 增益 0..4 | `1.0` |
| `-h`, `--help` | 帮助 | — |

录制结束输出统计：`acquired`、`new_desktop`、`encoded`、`packets`、`bytes`、`encode_errors`、`audio_packets`、`audio_bytes`、`elapsed`、`avg_fps`。

退出码：`0` 成功，`1` 失败（参数、初始化、采集/编码/封装错误）。

容器兼容性：HEVC 仅 mp4/mov/mkv；FLV 仅 H.264 且不支持 PCM；AVI/WMV 仅 H.264。不兼容组合会在启动前报错。
