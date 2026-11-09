# 开发历程与验证记录

本文记录项目的阶段性进展与验证结果；产品功能与使用方法见根目录 [README](../README.md)。

## Phase 0：采集 → 编码 → 封装

- 目标：DXGI Desktop Duplication → D3D11 BGRA→NV12 → AMD AMF HEVC/H.264 → FFmpeg MP4。
- 验证：2560×1440@60 连续录制 10 分钟，36,001 帧、59.99 fps、0 编码错误，全片解码无错误。
- 未覆盖：4K60、真实 HDR 显示器（合成测试覆盖色调映射算法）。
- 报告：[phase0-report.md](phase0-report.md)

## Phase 1：音频

- WASAPI 系统声 Loopback + 麦克风采集、环形缓冲、混音（增益/静音/软限幅）、AAC-LC 编码、MP4 音轨。
- 验证：10 秒 HEVC + AAC 48 kHz 立体声录制，全片解码 exit 0；麦克风为真实内容，系统无播放时 loopback 输出数字静音。
- 报告：[phase1-audio-report.md](phase1-audio-report.md)

## 多源采集与图形界面

- 采集抽象 `VideoSource`：全屏/区域（GPU 裁剪）、窗口（Windows Graphics Capture）、摄像头（Media Foundation）。
- 界面：无边框 Qt 窗口、实时预览、四种模式、区域拖拽框选与会话内确认按钮、窗口选择器。
- 容器：MP4 / MOV / MKV / AVI / FLV / WMV；音频 AAC / PCM、44.1/48 kHz、96–320 kbps。
- 音频设备枚举与选择、声音来源四选一。
- 设置：通用/录制/快捷键/输出/关于，QSettings 持久化；浅色/深色/跟随系统主题；开机自启、托盘、自定义快捷键。
- Logo 与 exe 图标、绿色版与 Inno Setup 安装包。

验证：`ctest` 107 项通过（摄像头 2 项因无硬件跳过）；各容器与音频组合逐项 ffmpeg 解码通过；安装包静默安装 → 录制 → 卸载全流程通过。

## 发布

- v0.1.0：首个可运行版本（多源录制、格式与音频选项、设置与主题）。
- v0.1.1：修复区域选择无法确认的问题，加入版本资源与发布打包。
- 发布页：https://github.com/7VVI/hmoob-ev/releases
