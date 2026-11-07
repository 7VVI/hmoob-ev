# Hmmob EV

Windows 高性能录屏软件。

当前阶段：Phase 0 — DXGI 4K60 抓屏 + AMD AMF 编码 + MP4 验证。

## 构建

```powershell
$cmake = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
& $cmake --preset windows-x64-release
& $cmake --build --preset windows-x64-release
.\build\release\Release\evspike.exe
```
