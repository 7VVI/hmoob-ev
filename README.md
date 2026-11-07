# Hmmob EV

Windows 高性能录屏软件。

当前阶段：Phase 0 — DXGI 4K60 抓屏 + AMD AMF 编码 + MP4 验证。

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
