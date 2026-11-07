# Hmmob EV Phase 0 实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在 Windows 上跑通 4K60 的 DXGI 抓屏 → D3D11 颜色转换 → AMD AMF 硬件编码 → MP4 落盘，验证整条录制管线的可行性和性能。

**Architecture:** 一个不依赖 Qt 的命令行 spike（`evspike`）+ 可复用的 `hmmob_core` 静态库。采集、颜色转换、编码、封装各自独立模块，通过简单接口连接，为 Phase 1 的产品化内核打基础。

**Tech Stack:** C++20、MSVC 2022、CMake、vcpkg、FFmpeg 9.x LGPL（vcpkg 当前基线）、D3D11、DXGI Desktop Duplication、AMD AMF SDK、Catch2。

---

## 0. 范围与交付物

本计划只覆盖 **Phase 0**，不包含 Qt UI、摄像头、音频、区域选择器、叠加层、RTMP、编辑器。

Phase 0 完成时，仓库里应有一个可执行文件：

```powershell
evspike.exe --output C:\temp\test.mp4 --seconds 30 --fps 60 --codec hevc --bitrate 50000000
```

它应做到：

- 捕获主显示器 4K 画面。
- 用 D3D11 VideoProcessor 将 BGRA 转 NV12。
- 用 AMD AMF 编码 HEVC/H.264。
- 用 FFmpeg libavformat 写 MP4。
- 输出统计：采集帧数、编码包数、丢帧数、平均 FPS、文件大小。
- HDR 显示器上能识别 HDR 格式并做基础色调映射（Task 10）。

> 本机只有 AMD GPU，Phase 0 只验证 AMF。NVENC/QSV 在后续有对应硬件的机器上验证。

## 1. 环境与依赖

本机已有：MSVC 14.44（VS 2022 BuildTools）、VS 自带 CMake/Ninja、Git 2.56、AMD Radeon RX 6750 GRE 12GB、`C:\Windows\System32\amfrt64.dll`。

需要安装：vcpkg（FFmpeg、Catch2）、FFmpeg 开发库（LGPL）、AMF 头文件。

### 1.1 vcpkg 初始化

```powershell
git clone https://github.com/microsoft/vcpkg.git C:\vcpkg
C:\vcpkg\bootstrap-vcpkg.bat
C:\vcpkg\vcpkg.exe version
```

预期：输出 vcpkg 版本号，无错误。

### 1.2 CMake 变量

```powershell
$cmake = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
```

---

## 2. 文件结构

```
hmmob-ev/
├─ .gitignore
├─ CMakeLists.txt
├─ CMakePresets.json
├─ vcpkg.json
├─ README.md
├─ cmake/FindFFmpeg.cmake
├─ scripts/fetch_amf.ps1
├─ third_party/amf/include        (gitignore 源码，只保留脚本)
├─ src/core/common/{Check.h,Log.h,Log.cpp,QpcClock.h,QpcClock.cpp,FrameQueue.h}
├─ src/core/gfx/{D3D11Device.h/.cpp,VideoProcessor.h/.cpp,HdrToneMapper.h/.cpp}
├─ src/core/capture/{DxgiCapture.h,DxgiCapture.cpp}
├─ src/core/encoder/{AmfEncoder.h,AmfEncoder.cpp}
├─ src/core/mux/{FfmpegMuxer.h,FfmpegMuxer.cpp}
├─ src/spike/main.cpp
├─ shaders/hdr_to_sdr.hlsl
└─ tests/{test_qpc_clock,test_frame_queue,test_d3d11_device,test_dxgi_capture,test_video_processor,test_amf_encoder}.cpp
```

---

## Task 1: 仓库骨架与最小构建

**Files:**
- Create: `.gitignore`, `CMakeLists.txt`, `CMakePresets.json`, `README.md`, `src/spike/main.cpp`

- [ ] **Step 1: 初始化仓库**

```powershell
cd C:\Users\Administrator\Desktop\project\hmmob-ev
git init
git add docs
git commit -m "docs: add Hmmob EV design spec and phase 0 plan"
```

- [ ] **Step 2: 写 `.gitignore`**

```gitignore
/build/
/out/
/.vs/
/.vscode/
/third_party/amf/src/
*.user
*.suo
*.mp4
*.mkv
*.h264
*.hevc
```

- [ ] **Step 3: 写 `CMakeLists.txt`**

```cmake
cmake_minimum_required(VERSION 3.29)
project(hmmob_ev VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
  add_compile_options(/W4 /permissive- /utf-8 /EHsc)
  add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
endif()

add_executable(evspike src/spike/main.cpp)
target_include_directories(evspike PRIVATE src)
```

- [ ] **Step 4: 写 `CMakePresets.json`**

```json
{
  "version": 6,
  "configurePresets": [
    {
      "name": "windows-x64-release",
      "displayName": "Windows x64 Release",
      "generator": "Visual Studio 17 2022",
      "architecture": "x64",
      "binaryDir": "${sourceDir}/build/release",
      "cacheVariables": {}
    }
  ],
  "buildPresets": [
    {
      "name": "windows-x64-release",
      "configurePreset": "windows-x64-release",
      "configuration": "Release"
    }
  ]
}
```

- [ ] **Step 5: 写 `src/spike/main.cpp`**

```cpp
#include <cstdio>

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    std::puts("hmmob-ev phase0 spike");
    return 0;
}
```

- [ ] **Step 6: 写 `README.md`**

````markdown
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
````

- [ ] **Step 7: 配置并构建**

```powershell
$cmake = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
& $cmake --preset windows-x64-release
& $cmake --build --preset windows-x64-release
```

预期：`build\release\Release\evspike.exe` 存在。

- [ ] **Step 8: 运行**

```powershell
.\build\release\Release\evspike.exe
```

预期输出：`hmmob-ev phase0 spike`。

- [ ] **Step 9: 提交**

```powershell
git add .gitignore CMakeLists.txt CMakePresets.json README.md src/spike/main.cpp
git commit -m "build: add phase 0 CMake skeleton"
```

---

## Task 2: vcpkg + FFmpeg 冒烟测试

> 实际实现说明：最终采用 vcpkg 自带的 `FindFFMPEG.cmake`（`find_package(FFMPEG REQUIRED)` + `${FFMPEG_LIBRARIES}`），并在 `vcpkg.json` 中 pin 了 `builtin-baseline`。下面的自定义 `FindFFmpeg.cmake` 步骤仅保留为历史记录，实际代码以提交 `12d35f1`、`3bfc66f`、`382935f` 为准。

**Files:**
- Create: `vcpkg.json`, `cmake/FindFFmpeg.cmake`
- Modify: `CMakeLists.txt`, `src/spike/main.cpp`

- [ ] **Step 1: 写 `vcpkg.json`**

```json
{
  "name": "hmmob-ev",
  "version-string": "0.1.0",
  "dependencies": [
    {
      "name": "ffmpeg",
      "default-features": false,
      "features": ["avcodec", "avformat", "swresample", "swscale"]
    },
    "catch2"
  ]
}
```

- [ ] **Step 2: 安装依赖**

```powershell
cd C:\Users\Administrator\Desktop\project\hmmob-ev
C:\vcpkg\vcpkg.exe install --triplet x64-windows
```

预期：FFmpeg 与 Catch2 安装完成。首次编译 FFmpeg 可能需要 10~30 分钟。

- [ ] **Step 2b: 在 `CMakePresets.json` 中加入 vcpkg 工具链**

把 `cacheVariables` 改为：

```json
      "cacheVariables": {
        "CMAKE_TOOLCHAIN_FILE": "C:/vcpkg/scripts/buildsystems/vcpkg.cmake",
        "VCPKG_TARGET_TRIPLET": "x64-windows"
      }
```

- [ ] **Step 3: 写 `cmake/FindFFmpeg.cmake`**

```cmake
find_path(AVCODEC_INCLUDE_DIR NAMES libavcodec/avcodec.h REQUIRED)
find_path(AVFORMAT_INCLUDE_DIR NAMES libavformat/avformat.h REQUIRED)
find_path(AVUTIL_INCLUDE_DIR NAMES libavutil/avutil.h REQUIRED)
find_path(SWSCALE_INCLUDE_DIR NAMES libswscale/swscale.h REQUIRED)
find_path(SWRESAMPLE_INCLUDE_DIR NAMES libswresample/swresample.h REQUIRED)

find_library(AVCODEC_LIBRARY NAMES avcodec REQUIRED)
find_library(AVFORMAT_LIBRARY NAMES avformat REQUIRED)
find_library(AVUTIL_LIBRARY NAMES avutil REQUIRED)
find_library(SWSCALE_LIBRARY NAMES swscale REQUIRED)
find_library(SWRESAMPLE_LIBRARY NAMES swresample REQUIRED)

function(hmmob_add_ffmpeg_target name library include_dir)
  add_library(FFmpeg::${name} UNKNOWN IMPORTED)
  set_target_properties(FFmpeg::${name} PROPERTIES
    IMPORTED_LOCATION "${library}"
    INTERFACE_INCLUDE_DIRECTORIES "${include_dir}")
endfunction()

hmmob_add_ffmpeg_target(avcodec "${AVCODEC_LIBRARY}" "${AVCODEC_INCLUDE_DIR}")
hmmob_add_ffmpeg_target(avformat "${AVFORMAT_LIBRARY}" "${AVFORMAT_INCLUDE_DIR}")
hmmob_add_ffmpeg_target(avutil "${AVUTIL_LIBRARY}" "${AVUTIL_INCLUDE_DIR}")
hmmob_add_ffmpeg_target(swscale "${SWSCALE_LIBRARY}" "${SWSCALE_INCLUDE_DIR}")
hmmob_add_ffmpeg_target(swresample "${SWRESAMPLE_LIBRARY}" "${SWRESAMPLE_INCLUDE_DIR}")
```

- [ ] **Step 4: 修改 `CMakeLists.txt`**

```cmake
cmake_minimum_required(VERSION 3.29)
project(hmmob_ev VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
  add_compile_options(/W4 /permissive- /utf-8 /EHsc)
  add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
find_package(FFmpeg REQUIRED)

add_executable(evspike src/spike/main.cpp)
target_include_directories(evspike PRIVATE src)
target_link_libraries(evspike PRIVATE
  FFmpeg::avcodec FFmpeg::avformat FFmpeg::avutil
  FFmpeg::swscale FFmpeg::swresample)

# Phase 0: vcpkg applocal copies FFmpeg DLLs next to evspike.exe.
# A proper DLL deployment step lands in Phase 1.
```

- [ ] **Step 5: 修改 `src/spike/main.cpp` 为 FFmpeg 冒烟测试**

```cpp
#include <cstdio>
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
}

int main() {
    std::printf("FFmpeg avcodec=%u avformat=%u avutil=%u\n",
                avcodec_version(), avformat_version(), avutil_version());
    return 0;
}
```

- [ ] **Step 6: 重新配置、构建、运行**

```powershell
$cmake = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
& $cmake --preset windows-x64-release
& $cmake --build --preset windows-x64-release
# vcpkg applocal normally copies FFmpeg DLLs next to evspike.exe.
# If the run fails with missing DLLs, prepend the actual vcpkg bin dir:
# $env:PATH = "<project>\build\release\vcpkg_installed\x64-windows\bin;$env:PATH"
.\build\release\Release\evspike.exe
```

预期：打印 FFmpeg 版本号。Phase 0 通过 PATH 加载 FFmpeg DLL；正式部署在 Phase 1 处理。

- [ ] **Step 7: 提交**

```powershell
git add vcpkg.json cmake/FindFFmpeg.cmake CMakeLists.txt src/spike/main.cpp
git commit -m "build: add vcpkg FFmpeg dependencies"
```

---

## Task 3: 时间与有界队列（TDD）

> 实际实现补充：`FrameQueue` 增加了 `waiter_count()`、析构契约断言、`[[nodiscard]]`、capacity 断言；测试增加了 Pop 超时、关闭后 drain、确定性 close-wake。`hmmob_core` 不链接 FFmpeg，FFmpeg 只由 `evspike` 链接。以实际提交为准。

**Files:**
- Create: `src/core/common/QpcClock.h/.cpp`, `src/core/common/FrameQueue.h`
- Create: `tests/test_qpc_clock.cpp`, `tests/test_frame_queue.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: 写失败测试 `tests/test_qpc_clock.cpp`**

```cpp
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <thread>
#include "core/common/QpcClock.h"

TEST_CASE("QpcClock returns monotonically increasing microseconds") {
    hmmob::QpcClock clock;
    const auto a = clock.NowUs();
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    const auto b = clock.NowUs();
    REQUIRE(b > a);
    REQUIRE(b - a >= 1000);
}
```

- [ ] **Step 2: 写失败测试 `tests/test_frame_queue.cpp`**

```cpp
#include <catch2/catch_test_macros.hpp>
#include <atomic>
#include <thread>
#include "core/common/FrameQueue.h"

TEST_CASE("FrameQueue pushes and pops in FIFO order") {
    hmmob::FrameQueue<int> q(4);
    REQUIRE(q.Push(1));
    REQUIRE(q.Push(2));
    int value = 0;
    REQUIRE(q.Pop(value, 10));
    REQUIRE(value == 1);
    REQUIRE(q.Pop(value, 10));
    REQUIRE(value == 2);
}

TEST_CASE("FrameQueue rejects when full") {
    hmmob::FrameQueue<int> q(1);
    REQUIRE(q.Push(1));
    REQUIRE_FALSE(q.Push(2));
}

TEST_CASE("FrameQueue close wakes a blocked pop") {
    hmmob::FrameQueue<int> q(1);
    std::atomic<bool> pop_result{true};
    std::thread t([&] {
        int value = 0;
        pop_result = q.Pop(value, 5000);
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    q.Close();
    t.join();
    REQUIRE_FALSE(pop_result.load());
}

TEST_CASE("FrameQueue Size tracks queued items") {
    hmmob::FrameQueue<int> q(4);
    REQUIRE(q.Size() == 0);
    REQUIRE(q.Push(1));
    REQUIRE(q.Push(2));
    REQUIRE(q.Size() == 2);
    int value = 0;
    REQUIRE(q.Pop(value, 10));
    REQUIRE(q.Size() == 1);
}

TEST_CASE("FrameQueue rejects push after close") {
    hmmob::FrameQueue<int> q(2);
    q.Close();
    REQUIRE_FALSE(q.Push(1));
}
```

- [ ] **Step 3: 修改 `CMakeLists.txt`**

```cmake
add_library(hmmob_core STATIC
  src/core/common/QpcClock.cpp
)
target_include_directories(hmmob_core PUBLIC src)

add_executable(evspike src/spike/main.cpp)
target_include_directories(evspike SYSTEM PRIVATE ${FFMPEG_INCLUDE_DIRS})
target_link_libraries(evspike PRIVATE hmmob_core ${FFMPEG_LIBRARIES})

find_package(Catch2 3 REQUIRED)
enable_testing()
add_executable(hmmob_tests
  tests/test_qpc_clock.cpp
  tests/test_frame_queue.cpp
)
target_link_libraries(hmmob_tests PRIVATE hmmob_core Catch2::Catch2WithMain)
include(Catch)
catch_discover_tests(hmmob_tests)
```

- [ ] **Step 4: 运行测试确认失败**

```powershell
& $cmake --preset windows-x64-release
& $cmake --build --preset windows-x64-release
```

预期：编译失败，提示找不到 `QpcClock.h` / `FrameQueue.h`。

- [ ] **Step 5: 实现 `src/core/common/QpcClock.h`**

```cpp
#pragma once
#include <cstdint>

namespace hmmob {

class QpcClock {
public:
    QpcClock();
    int64_t NowUs() const;

private:
    int64_t frequency_ = 0;
};

}  // namespace hmmob
```

- [ ] **Step 6: 实现 `src/core/common/QpcClock.cpp`**

```cpp
#include "core/common/QpcClock.h"
#include <windows.h>

namespace hmmob {

QpcClock::QpcClock() {
    LARGE_INTEGER frequency{};
    QueryPerformanceFrequency(&frequency);
    frequency_ = frequency.QuadPart;
}

int64_t QpcClock::NowUs() const {
    if (frequency_ <= 0) {
        return 0;
    }
    LARGE_INTEGER counter{};
    QueryPerformanceCounter(&counter);
    const int64_t seconds = counter.QuadPart / frequency_;
    const int64_t remainder = counter.QuadPart % frequency_;
    return seconds * 1'000'000 + (remainder * 1'000'000) / frequency_;
}

}  // namespace hmmob
```

- [ ] **Step 7: 实现 `src/core/common/FrameQueue.h`**

```cpp
#pragma once
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <utility>

namespace hmmob {

template <typename T>
class FrameQueue {
public:
    explicit FrameQueue(size_t capacity) : capacity_(capacity) {}

    bool Push(T item) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (closed_ || queue_.size() >= capacity_) {
            return false;
        }
        queue_.push_back(std::move(item));
        cv_.notify_one();
        return true;
    }

    bool Pop(T& out, int timeout_ms) {
        std::unique_lock<std::mutex> lock(mutex_);
        if (!cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms),
                          [this] { return closed_ || !queue_.empty(); })) {
            return false;
        }
        if (queue_.empty()) {
            return false;
        }
        out = std::move(queue_.front());
        queue_.pop_front();
        return true;
    }

    void Close() {
        std::lock_guard<std::mutex> lock(mutex_);
        closed_ = true;
        cv_.notify_all();
    }

    size_t Size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.size();
    }

private:
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<T> queue_;
    size_t capacity_;
    bool closed_ = false;
};

}  // namespace hmmob
```

- [ ] **Step 8: 运行测试确认通过**

```powershell
& $cmake --build --preset windows-x64-release
ctest --test-dir build/release -C Release --output-on-failure
```

预期：`100% tests passed`。

- [ ] **Step 9: 提交**

```powershell
git add src/core/common tests/test_qpc_clock.cpp tests/test_frame_queue.cpp CMakeLists.txt
git commit -m "feat: add qpc clock and bounded frame queue"
```

---

## Task 4: D3D11 设备与显示器枚举

> 实际实现补充：`DisplayInfo` 增加 `adapter_index`、`output_index`、`attached_to_desktop`、`rotation`、亮度与色深字段；`EnumerateDisplays` 遍历所有 DXGI adapter；`Create()` 失败清理并校验 feature level；HDR 判定抽成 `IsHdrColorSpace` / `FromDesc1` 并有 GPU-free 测试。以实际提交为准。

**Files:**
- Create: `src/core/common/Check.h`, `src/core/gfx/D3D11Device.h/.cpp`, `tests/test_d3d11_device.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: 写失败测试 `tests/test_d3d11_device.cpp`**

```cpp
#include <catch2/catch_test_macros.hpp>
#include "core/gfx/D3D11Device.h"

TEST_CASE("D3D11 device creates and enumerates at least one display") {
    hmmob::D3D11Device d3d;
    REQUIRE(d3d.Create());
    const auto displays = d3d.EnumerateDisplays();
    REQUIRE_FALSE(displays.empty());
    for (const auto& d : displays) {
        REQUIRE(d.width > 0);
        REQUIRE(d.height > 0);
    }
}
```

- [ ] **Step 2: 修改 `CMakeLists.txt`**

```cmake
add_library(hmmob_core STATIC
  src/core/common/QpcClock.cpp
  src/core/gfx/D3D11Device.cpp
)
target_link_libraries(hmmob_core PUBLIC d3d11 dxgi)

# hmmob_tests 源文件加入 tests/test_d3d11_device.cpp
```

- [ ] **Step 3: 编译确认失败**

预期：找不到 `core/gfx/D3D11Device.h`。

- [ ] **Step 4: 写 `src/core/common/Check.h`**

```cpp
#pragma once
#include <cstdio>
#include <windows.h>

#define HMMOB_HR_CHECK(expr)                                                   \
    do {                                                                       \
        HRESULT hmmob_hr_ = (expr);                                                 \
        if (FAILED(hmmob_hr_)) {                                                    \
            std::fprintf(stderr, "%s:%d HRESULT=0x%08lx\n", __FILE__, __LINE__, \
                         static_cast<unsigned long>(hmmob_hr_));                    \
            return false;                                                      \
        }                                                                      \
    } while (0)

#define HMMOB_HR_CHECK_PTR(expr)                                               \
    do {                                                                       \
        HRESULT hmmob_hr_ = (expr);                                                 \
        if (FAILED(hmmob_hr_)) {                                                    \
            std::fprintf(stderr, "%s:%d HRESULT=0x%08lx\n", __FILE__, __LINE__, \
                         static_cast<unsigned long>(hmmob_hr_));                    \
            return nullptr;                                                    \
        }                                                                      \
    } while (0)
```

- [ ] **Step 5: 写 `src/core/gfx/D3D11Device.h`**

```cpp
#pragma once
#include <d3d11.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <string>
#include <vector>

namespace hmmob {

using Microsoft::WRL::ComPtr;

struct DisplayInfo {
    int index = 0;
    std::wstring name;
    int width = 0;
    int height = 0;
    bool hdr = false;
    DXGI_COLOR_SPACE_TYPE color_space = DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
};

class D3D11Device {
public:
    bool Create();
    ID3D11Device* device() const { return device_.Get(); }
    ID3D11DeviceContext* context() const { return context_.Get(); }
    IDXGIFactory2* factory() const { return factory_.Get(); }
    std::vector<DisplayInfo> EnumerateDisplays() const;

private:
    ComPtr<ID3D11Device> device_;
    ComPtr<ID3D11DeviceContext> context_;
    ComPtr<IDXGIFactory2> factory_;
};

}  // namespace hmmob
```

- [ ] **Step 6: 写 `src/core/gfx/D3D11Device.cpp`**

```cpp
#include "core/gfx/D3D11Device.h"
#include "core/common/Check.h"
#include <cstdlib>

namespace hmmob {

bool D3D11Device::Create() {
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
    D3D_FEATURE_LEVEL chosen{};
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
                                   flags, levels, _countof(levels),
                                   D3D11_SDK_VERSION, &device_, &chosen, &context_);
    if (FAILED(hr)) {
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
                               D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0,
                               D3D11_SDK_VERSION, &device_, &chosen, &context_);
    }
    HMMOB_HR_CHECK(hr);

    ComPtr<IDXGIDevice> dxgi_device;
    HMMOB_HR_CHECK(device_.As(&dxgi_device));
    ComPtr<IDXGIAdapter> adapter;
    HMMOB_HR_CHECK(dxgi_device->GetAdapter(&adapter));
    ComPtr<IDXGIFactory2> factory;
    HMMOB_HR_CHECK(adapter->GetParent(IID_PPV_ARGS(&factory)));
    factory_ = factory;
    return true;
}

std::vector<DisplayInfo> D3D11Device::EnumerateDisplays() const {
    std::vector<DisplayInfo> result;
    if (!device_) {
        return result;
    }

    ComPtr<IDXGIDevice> dxgi_device;
    if (FAILED(device_.As(&dxgi_device))) {
        return result;
    }
    ComPtr<IDXGIAdapter> adapter;
    if (FAILED(dxgi_device->GetAdapter(&adapter))) {
        return result;
    }

    for (UINT i = 0;; ++i) {
        ComPtr<IDXGIOutput> output;
        if (adapter->EnumOutputs(i, &output) == DXGI_ERROR_NOT_FOUND) {
            break;
        }
        if (!output) {
            break;
        }

        DisplayInfo info;
        info.index = static_cast<int>(i);

        ComPtr<IDXGIOutput6> output6;
        if (SUCCEEDED(output.As(&output6))) {
            DXGI_OUTPUT_DESC1 desc1{};
            if (SUCCEEDED(output6->GetDesc1(&desc1))) {
                info.name = desc1.DeviceName;
                info.width = desc1.DesktopCoordinates.right - desc1.DesktopCoordinates.left;
                info.height = desc1.DesktopCoordinates.bottom - desc1.DesktopCoordinates.top;
                info.color_space = desc1.ColorSpace;
                info.hdr = desc1.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020 ||
                           desc1.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709;
                result.push_back(info);
                continue;
            }
        }

        DXGI_OUTPUT_DESC desc{};
        if (SUCCEEDED(output->GetDesc(&desc))) {
            info.name = desc.DeviceName;
            info.width = desc.DesktopCoordinates.right - desc.DesktopCoordinates.left;
            info.height = desc.DesktopCoordinates.bottom - desc.DesktopCoordinates.top;
            result.push_back(info);
        }
    }
    return result;
}

}  // namespace hmmob
```

- [ ] **Step 7: 构建并运行测试**

```powershell
& $cmake --preset windows-x64-release
& $cmake --build --preset windows-x64-release
ctest --test-dir build/release -C Release --output-on-failure
```

预期：D3D11 测试通过，至少枚举到一个显示器。

- [ ] **Step 8: 提交**

```powershell
git add src/core/common/Check.h src/core/gfx/D3D11Device.* tests/test_d3d11_device.cpp CMakeLists.txt
git commit -m "feat: add d3d11 device and display enumeration"
```

---

## Task 5: DXGI Desktop Duplication 采集

**Files:**
- Create: `src/core/capture/DxgiCapture.h/.cpp`, `tests/test_dxgi_capture.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: 写失败测试 `tests/test_dxgi_capture.cpp`**

```cpp
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <windows.h>
#include "core/capture/DxgiCapture.h"

TEST_CASE("DXGI capture acquires frames from the primary display", "[integration]") {
    hmmob::D3D11Device d3d;
    REQUIRE(d3d.Create());
    if (d3d.EnumerateDisplays().empty()) {
        SKIP("no display available");
    }

    hmmob::DxgiCapture capture;
    if (!capture.Initialize(d3d, 0, false)) {
        SKIP("desktop duplication unavailable");
    }

    POINT original{};
    GetCursorPos(&original);

    int frames = 0;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    for (int i = 0; frames < 20 && std::chrono::steady_clock::now() < deadline; ++i) {
        // Desktop Duplication only produces a frame when the desktop changes.
        // Nudge the cursor so a static desktop still generates frames.
        SetCursorPos(original.x + (i % 2 ? 1 : -1), original.y);
        hmmob::CaptureFrame frame;
        if (capture.Acquire(frame, 250)) {
            REQUIRE(frame.texture);
            REQUIRE(frame.width > 0);
            REQUIRE(frame.height > 0);
            ++frames;
        }
    }
    SetCursorPos(original.x, original.y);
    REQUIRE(frames > 0);
}
```

- [ ] **Step 2: 修改 `CMakeLists.txt`**

```cmake
add_library(hmmob_core STATIC
  src/core/common/QpcClock.cpp
  src/core/gfx/D3D11Device.cpp
  src/core/capture/DxgiCapture.cpp
)
# hmmob_tests 源文件加入 tests/test_dxgi_capture.cpp
```

- [ ] **Step 3: 编译确认失败**

预期：找不到 `DxgiCapture.h`。

- [ ] **Step 4: 写 `src/core/capture/DxgiCapture.h`**

```cpp
#pragma once
#include "core/common/QpcClock.h"
#include "core/gfx/D3D11Device.h"
#include <dxgi1_5.h>
#include <wrl/client.h>
#include <cstdint>

namespace hmmob {

struct CaptureFrame {
    // Reused by the next Acquire; consume before calling Acquire again.
    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
    int64_t pts_us = 0;
    int width = 0;
    int height = 0;
    bool hdr = false;
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
};

class DxgiCapture {
public:
    bool Initialize(D3D11Device& d3d, int display_index, bool request_hdr);
    bool Acquire(CaptureFrame& frame, int timeout_ms);
    void Shutdown();

    int width() const { return width_; }
    int height() const { return height_; }
    DXGI_FORMAT format() const { return format_; }
    bool hdr() const { return hdr_; }
    bool access_lost() const { return access_lost_; }

private:
    bool CreateDuplication();

    D3D11Device* d3d_ = nullptr;
    int display_index_ = 0;
    bool request_hdr_ = false;
    bool access_lost_ = false;

    Microsoft::WRL::ComPtr<IDXGIOutput1> output1_;
    Microsoft::WRL::ComPtr<IDXGIOutputDuplication> duplication_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> copy_texture_;

    int width_ = 0;
    int height_ = 0;
    bool hdr_ = false;
    DXGI_FORMAT format_ = DXGI_FORMAT_B8G8R8A8_UNORM;
    QpcClock clock_;
};

}  // namespace hmmob
```

- [ ] **Step 5: 写 `src/core/capture/DxgiCapture.cpp`**

```cpp
#include "core/capture/DxgiCapture.h"
#include "core/common/Check.h"

namespace hmmob {

bool DxgiCapture::Initialize(D3D11Device& d3d, int display_index, bool request_hdr) {
    d3d_ = &d3d;
    display_index_ = display_index;
    request_hdr_ = request_hdr;
    return CreateDuplication();
}

bool DxgiCapture::CreateDuplication() {
    access_lost_ = false;
    duplication_.Reset();
    output1_.Reset();
    copy_texture_.Reset();

    const auto displays = d3d_->EnumerateDisplays();
    if (display_index_ < 0 || display_index_ >= static_cast<int>(displays.size())) {
        return false;
    }
    const auto& display = displays[display_index_];
    // Phase 0: the D3D11 device must be on the display's adapter;
    // display 0 is on the default adapter.
    ComPtr<IDXGIAdapter1> adapter;
    HMMOB_HR_CHECK(d3d_->factory()->EnumAdapters1(
        static_cast<UINT>(display.adapter_index), &adapter));
    ComPtr<IDXGIOutput> output;
    HMMOB_HR_CHECK(adapter->EnumOutputs(
        static_cast<UINT>(display.output_index), &output));
    HMMOB_HR_CHECK(output.As(&output1_));

    DXGI_OUTPUT_DESC desc{};
    HMMOB_HR_CHECK(output1_->GetDesc(&desc));
    width_ = desc.DesktopCoordinates.right - desc.DesktopCoordinates.left;
    height_ = desc.DesktopCoordinates.bottom - desc.DesktopCoordinates.top;

    ComPtr<IDXGIOutput5> output5;
    if (SUCCEEDED(output.As(&output5))) {
        DXGI_FORMAT formats[3];
        UINT count = 0;
        if (request_hdr_) {
            formats[count++] = DXGI_FORMAT_R16G16B16A16_FLOAT;
            formats[count++] = DXGI_FORMAT_R10G10B10A2_UNORM;
        }
        formats[count++] = DXGI_FORMAT_B8G8R8A8_UNORM;

        HRESULT hr = output5->DuplicateOutput1(d3d_->device(), 0, count, formats, &duplication_);
        if (FAILED(hr)) {
            duplication_.Reset();
        }
    }

    if (!duplication_) {
        HMMOB_HR_CHECK(output1_->DuplicateOutput(d3d_->device(), &duplication_));
    }

    DXGI_OUTDUPL_DESC dupl_desc{};
    duplication_->GetDesc(&dupl_desc);
    format_ = dupl_desc.ModeDesc.Format;
    hdr_ = format_ == DXGI_FORMAT_R16G16B16A16_FLOAT ||
           format_ == DXGI_FORMAT_R10G10B10A2_UNORM;

    D3D11_TEXTURE2D_DESC tex_desc{};
    tex_desc.Width = static_cast<UINT>(width_);
    tex_desc.Height = static_cast<UINT>(height_);
    tex_desc.MipLevels = 1;
    tex_desc.ArraySize = 1;
    tex_desc.Format = format_;
    tex_desc.SampleDesc.Count = 1;
    tex_desc.Usage = D3D11_USAGE_DEFAULT;
    tex_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    HMMOB_HR_CHECK(d3d_->device()->CreateTexture2D(&tex_desc, nullptr, &copy_texture_));
    return true;
}

bool DxgiCapture::Acquire(CaptureFrame& frame, int timeout_ms) {
    if (!duplication_) {
        if (!CreateDuplication()) {
            return false;
        }
    }

    DXGI_OUTDUPL_FRAME_INFO info{};
    ComPtr<IDXGIResource> resource;
    HRESULT hr = duplication_->AcquireNextFrame(static_cast<UINT>(timeout_ms), &info, &resource);
    if (hr == DXGI_ERROR_WAIT_TIMEOUT) {
        return false;
    }
    if (hr == DXGI_ERROR_ACCESS_LOST) {
        access_lost_ = true;
        duplication_.Reset();
        return false;
    }
    HMMOB_HR_CHECK(hr);

    ComPtr<ID3D11Texture2D> texture;
    if (FAILED(resource.As(&texture))) {
        duplication_->ReleaseFrame();
        return false;
    }
    d3d_->context()->CopyResource(copy_texture_.Get(), texture.Get());
    duplication_->ReleaseFrame();

    frame.texture = copy_texture_;
    frame.pts_us = clock_.NowUs();
    frame.width = width_;
    frame.height = height_;
    frame.hdr = hdr_;
    frame.format = format_;
    return true;
}

void DxgiCapture::Shutdown() {
    duplication_.Reset();
    output1_.Reset();
    copy_texture_.Reset();
}

}  // namespace hmmob
```

- [ ] **Step 6: 构建并运行集成测试**

```powershell
& $cmake --preset windows-x64-release
& $cmake --build --preset windows-x64-release
ctest --test-dir build/release -C Release --output-on-failure
```

预期：`DXGI capture acquires frames` 通过；没有显示器时 SKIP。

- [ ] **Step 7: 提交**

```powershell
git add src/core/capture/DxgiCapture.* tests/test_dxgi_capture.cpp CMakeLists.txt
git commit -m "feat: add dxgi desktop duplication capture"
```

---

## Task 6: BGRA → NV12 GPU 颜色转换

**Files:**
- Create: `src/core/gfx/VideoProcessor.h/.cpp`, `tests/test_video_processor.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: 写失败测试 `tests/test_video_processor.cpp`**

```cpp
#include <catch2/catch_test_macros.hpp>
#include <vector>
#include "core/gfx/VideoProcessor.h"

TEST_CASE("VideoProcessor converts BGRA to NV12", "[integration]") {
    hmmob::D3D11Device d3d;
    REQUIRE(d3d.Create());

    constexpr int kWidth = 640;
    constexpr int kHeight = 360;

    D3D11_TEXTURE2D_DESC src_desc{};
    src_desc.Width = kWidth;
    src_desc.Height = kHeight;
    src_desc.MipLevels = 1;
    src_desc.ArraySize = 1;
    src_desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    src_desc.SampleDesc.Count = 1;
    src_desc.Usage = D3D11_USAGE_DEFAULT;
    src_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;

    std::vector<uint8_t> bgra(kWidth * kHeight * 4, 128);
    D3D11_SUBRESOURCE_DATA init{};
    init.pSysMem = bgra.data();
    init.SysMemPitch = kWidth * 4;

    Microsoft::WRL::ComPtr<ID3D11Texture2D> src;
    REQUIRE(SUCCEEDED(d3d.device()->CreateTexture2D(&src_desc, &init, &src)));

    hmmob::VideoProcessor processor;
    REQUIRE(processor.Initialize(d3d, kWidth, kHeight));
    REQUIRE(processor.Convert(src.Get(), processor.output_texture()));

    D3D11_TEXTURE2D_DESC out_desc{};
    processor.output_texture()->GetDesc(&out_desc);
    REQUIRE(out_desc.Format == DXGI_FORMAT_NV12);

    D3D11_TEXTURE2D_DESC staging_desc = out_desc;
    staging_desc.Usage = D3D11_USAGE_STAGING;
    staging_desc.BindFlags = 0;
    staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> staging;
    REQUIRE(SUCCEEDED(d3d.device()->CreateTexture2D(&staging_desc, nullptr, &staging)));
    d3d.context()->CopyResource(staging.Get(), processor.output_texture());

    D3D11_MAPPED_SUBRESOURCE mapped{};
    REQUIRE(SUCCEEDED(d3d.context()->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)));
    const uint8_t y = static_cast<const uint8_t*>(mapped.pData)[0];
    d3d.context()->Unmap(staging.Get(), 0);
    REQUIRE(y > 80);
    REQUIRE(y < 180);
}
```

- [ ] **Step 2: 修改 `CMakeLists.txt`**

```cmake
add_library(hmmob_core STATIC
  ...
  src/core/gfx/VideoProcessor.cpp
)
# hmmob_tests 源文件加入 tests/test_video_processor.cpp
```

- [ ] **Step 3: 编译确认失败**

预期：找不到 `VideoProcessor.h`。

- [ ] **Step 4: 写 `src/core/gfx/VideoProcessor.h`**

```cpp
#pragma once
#include "core/gfx/D3D11Device.h"
#include <d3d11.h>
#include <wrl/client.h>

namespace hmmob {

class VideoProcessor {
public:
    bool Initialize(D3D11Device& d3d, int width, int height);
    bool Convert(ID3D11Texture2D* bgra, ID3D11Texture2D* nv12);
    ID3D11Texture2D* output_texture() const { return output_.Get(); }

private:
    ID3D11Device* device_ = nullptr;
    ID3D11DeviceContext* context_ = nullptr;
    Microsoft::WRL::ComPtr<ID3D11VideoDevice> video_device_;
    Microsoft::WRL::ComPtr<ID3D11VideoContext> video_context_;
    Microsoft::WRL::ComPtr<ID3D11VideoProcessorEnumerator> enumerator_;
    Microsoft::WRL::ComPtr<ID3D11VideoProcessor> processor_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> output_;
    int width_ = 0;
    int height_ = 0;
};

}  // namespace hmmob
```

- [ ] **Step 5: 写 `src/core/gfx/VideoProcessor.cpp`**

```cpp
#include "core/gfx/VideoProcessor.h"
#include "core/common/Check.h"

namespace hmmob {

bool VideoProcessor::Initialize(D3D11Device& d3d, int width, int height) {
    device_ = d3d.device();
    context_ = d3d.context();
    width_ = width;
    height_ = height;

    HMMOB_HR_CHECK(device_->QueryInterface(IID_PPV_ARGS(&video_device_)));
    HMMOB_HR_CHECK(context_->QueryInterface(IID_PPV_ARGS(&video_context_)));

    D3D11_VIDEO_PROCESSOR_CONTENT_DESC content{};
    content.InputFrameFormat = D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;
    content.InputFrameRate = {60, 1};
    content.InputWidth = static_cast<UINT>(width);
    content.InputHeight = static_cast<UINT>(height);
    content.OutputFrameRate = {60, 1};
    content.OutputWidth = static_cast<UINT>(width);
    content.OutputHeight = static_cast<UINT>(height);
    content.Usage = D3D11_VIDEO_USAGE_PLAYBACK_NORMAL;

    HMMOB_HR_CHECK(video_device_->CreateVideoProcessorEnumerator(&content, &enumerator_));
    HMMOB_HR_CHECK(video_device_->CreateVideoProcessor(enumerator_.Get(), 0, &processor_));

    D3D11_TEXTURE2D_DESC out_desc{};
    out_desc.Width = static_cast<UINT>(width);
    out_desc.Height = static_cast<UINT>(height);
    out_desc.MipLevels = 1;
    out_desc.ArraySize = 1;
    out_desc.Format = DXGI_FORMAT_NV12;
    out_desc.SampleDesc.Count = 1;
    out_desc.Usage = D3D11_USAGE_DEFAULT;
    out_desc.BindFlags = D3D11_BIND_RENDER_TARGET;
    HMMOB_HR_CHECK(device_->CreateTexture2D(&out_desc, nullptr, &output_));

    DXGI_COLOR_SPACE_TYPE input_cs = DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
    video_context_->VideoProcessorSetStreamColorSpace(processor_.Get(), 0, input_cs);
    DXGI_COLOR_SPACE_TYPE output_cs = DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P709;
    video_context_->VideoProcessorSetOutputColorSpace(processor_.Get(), &output_cs);
    return true;
}

bool VideoProcessor::Convert(ID3D11Texture2D* bgra, ID3D11Texture2D* nv12) {
    if (!processor_ || !bgra || !nv12) {
        return false;
    }

    D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC input_desc{};
    input_desc.FourCC = 0;
    input_desc.ViewDimension = D3D11_VPIV_DIMENSION_TEXTURE2D;
    input_desc.Texture2D.MipSlice = 0;
    input_desc.Texture2D.ArraySlice = 0;

    Microsoft::WRL::ComPtr<ID3D11VideoProcessorInputView> input_view;
    HMMOB_HR_CHECK(video_device_->CreateVideoProcessorInputView(
        bgra, enumerator_.Get(), &input_desc, &input_view));

    D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC out_desc{};
    out_desc.ViewDimension = D3D11_VPOV_DIMENSION_TEXTURE2D;
    out_desc.Texture2D.MipSlice = 0;
    Microsoft::WRL::ComPtr<ID3D11VideoProcessorOutputView> out_view;
    HMMOB_HR_CHECK(video_device_->CreateVideoProcessorOutputView(
        nv12, enumerator_.Get(), &out_desc, &out_view));

    D3D11_VIDEO_PROCESSOR_STREAM stream{};
    stream.Enable = TRUE;
    stream.pInputSurface = input_view.Get();

    HMMOB_HR_CHECK(video_context_->VideoProcessorBlt(
        processor_.Get(), out_view.Get(), 0, 1, &stream));
    return true;
}

}  // namespace hmmob
```

- [ ] **Step 6: 构建并运行测试**

```powershell
& $cmake --preset windows-x64-release
& $cmake --build --preset windows-x64-release
ctest --test-dir build/release -C Release --output-on-failure
```

预期：NV12 转换测试通过，Y 值在 80~180 之间。

- [ ] **Step 7: 提交**

```powershell
git add src/core/gfx/VideoProcessor.* tests/test_video_processor.cpp CMakeLists.txt
git commit -m "feat: add bgra to nv12 gpu conversion"
```

---

## Task 7: AMD AMF 编码器

**Files:**
- Create: `scripts/fetch_amf.ps1`, `src/core/encoder/AmfEncoder.h/.cpp`, `tests/test_amf_encoder.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: 写 `scripts/fetch_amf.ps1`**

```powershell
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$dest = Join-Path $root "third_party\amf"
$include = Join-Path $dest "include\components\VideoEncoderHEVC.h"

if (Test-Path $include) {
    Write-Host "AMF headers already present"
    exit 0
}

New-Item -ItemType Directory -Force -Path $dest | Out-Null
$src = Join-Path $dest "src"
if (Test-Path $src) { Remove-Item $src -Recurse -Force }
git clone --depth 1 https://github.com/GPUOpen-LibrariesAndSDKs/AMF.git $src
Copy-Item (Join-Path $src "amf\public\include") (Join-Path $dest "include") -Recurse
Remove-Item $src -Recurse -Force
Write-Host "AMF headers installed to $dest\include"
```

- [ ] **Step 2: 拉取 AMF 头文件**

```powershell
cd C:\Users\Administrator\Desktop\project\hmmob-ev
powershell -ExecutionPolicy Bypass -File .\scripts\fetch_amf.ps1
```

预期：`third_party\amf\include\components\VideoEncoderHEVC.h` 存在。

- [ ] **Step 3: 修改 `CMakeLists.txt`**

```cmake
add_library(hmmob_core STATIC
  ...
  src/core/encoder/AmfEncoder.cpp
)
target_include_directories(hmmob_core PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/third_party/amf/include")
# hmmob_tests 源文件加入 tests/test_amf_encoder.cpp
```

- [ ] **Step 4: 写失败测试 `tests/test_amf_encoder.cpp`**

```cpp
#include <catch2/catch_test_macros.hpp>
#include <vector>
#include "core/encoder/AmfEncoder.h"

TEST_CASE("AMF encoder produces HEVC packets", "[integration][amf]") {
    hmmob::D3D11Device d3d;
    REQUIRE(d3d.Create());

    hmmob::AmfEncoder encoder;
    if (!encoder.Initialize(d3d.device(), "hevc", 1920, 1080, 60, 10'000'000)) {
        SKIP("AMF encoder unavailable");
    }

    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = 1920;
    desc.Height = 1080;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_NV12;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

    std::vector<uint8_t> nv12(1920 * 1080 * 3 / 2, 128);
    D3D11_SUBRESOURCE_DATA init{};
    init.pSysMem = nv12.data();
    init.SysMemPitch = 1920;
    init.SysMemSlicePitch = 1920 * 1080;

    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
    REQUIRE(SUCCEEDED(d3d.device()->CreateTexture2D(&desc, &init, &texture)));

    int packets = 0;
    size_t bytes = 0;
    for (int i = 0; i < 30; ++i) {
        REQUIRE(encoder.Encode(texture.Get(), i * 16667, [&](const hmmob::AmfPacket& packet) {
            ++packets;
            bytes += packet.data.size();
        }));
    }
    REQUIRE(encoder.Flush([&](const hmmob::AmfPacket& packet) {
        ++packets;
        bytes += packet.data.size();
    }));
    REQUIRE(packets > 0);
    REQUIRE(bytes > 0);
}
```

- [ ] **Step 5: 写 `src/core/encoder/AmfEncoder.h`**

```cpp
#pragma once
#include <d3d11.h>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace hmmob {

struct AmfPacket {
    std::vector<uint8_t> data;
    int64_t pts_us = 0;
    bool keyframe = false;
};

class AmfEncoder {
public:
    using PacketCallback = std::function<void(const AmfPacket&)>;

    AmfEncoder() = default;
    ~AmfEncoder();
    AmfEncoder(const AmfEncoder&) = delete;
    AmfEncoder& operator=(const AmfEncoder&) = delete;

    bool Initialize(ID3D11Device* device, const std::string& codec,
                    int width, int height, int fps, int bitrate);
    bool Encode(ID3D11Texture2D* nv12_texture, int64_t pts_us, const PacketCallback& callback);
    bool Flush(const PacketCallback& callback);
    void Shutdown();
    bool is_initialized() const;

private:
    struct Impl;
    Impl* impl_ = nullptr;
};

}  // namespace hmmob
```

- [ ] **Step 6: 写 `src/core/encoder/AmfEncoder.cpp`**

```cpp
#include "core/encoder/AmfEncoder.h"

#include <windows.h>
#include <AMF/core/Factory.h>
#include <AMF/core/Context.h>
#include <AMF/core/Surface.h>
#include <AMF/core/Buffer.h>
#include <AMF/components/VideoEncoderHEVC.h>
#include <AMF/components/VideoEncoderH264.h>
#include <AMF/components/VideoEncoderVCE.h>
#include <cstdio>

namespace hmmob {

namespace {
using AMFInitFn = AMF_RESULT(AMF_STD_CALL*)(amf_uint64, amf::AMFFactory**);

void LogAmfError(const char* what, AMF_RESULT result) {
    std::fprintf(stderr, "AMF %s failed: %d\n", what, static_cast<int>(result));
}
}  // namespace

struct AmfEncoder::Impl {
    HMODULE module = nullptr;
    amf::AMFFactory* factory = nullptr;
    amf::AMFContextPtr context;
    amf::AMFComponentPtr encoder;
    int width = 0;
    int height = 0;
    int fps = 0;
    std::string codec;

    bool Drain(const PacketCallback& callback) {
        for (;;) {
            amf::AMFDataPtr data;
            AMF_RESULT result = encoder->QueryOutput(&data);
            if (result == AMF_EOF) {
                return true;
            }
            if (result != AMF_OK || !data) {
                return result == AMF_OK;
            }

            amf::AMFBufferPtr buffer(data);
            if (!buffer) {
                continue;
            }

            AmfPacket packet;
            packet.pts_us = data->GetPts();
            const auto* bytes = static_cast<const uint8_t*>(buffer->GetNative());
            packet.data.assign(bytes, bytes + buffer->GetSize());

            amf_int64 type = 0;
            if (data->GetProperty(AMF_VIDEO_ENCODER_OUTPUT_DATA_TYPE, &type) == AMF_OK) {
                packet.keyframe = type == AMF_VIDEO_ENCODER_OUTPUT_DATA_TYPE_IDR ||
                                  type == AMF_VIDEO_ENCODER_OUTPUT_DATA_TYPE_I;
            }
            callback(packet);
        }
    }

    bool CreateEncoder(const std::string& codec_name, int bitrate) {
        codec = codec_name;
        const wchar_t* component = codec == "hevc" ? AMFVideoEncoder_HEVC
                                                   : AMFVideoEncoder_H264;
        AMF_RESULT result = factory->CreateComponent(context, component, &encoder);
        if (result != AMF_OK) {
            LogAmfError("CreateComponent", result);
            return false;
        }

        encoder->SetProperty(AMF_VIDEO_ENCODER_USAGE,
                             AMF_VIDEO_ENCODER_USAGE_TRANSCODING);
        encoder->SetProperty(AMF_VIDEO_ENCODER_TARGET_BITRATE,
                             static_cast<amf_int64>(bitrate));
        encoder->SetProperty(AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD,
                             AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD_CBR);
        encoder->SetProperty(AMF_VIDEO_ENCODER_FRAMERATE,
                             AMFConstructRate(fps, 1));
        encoder->SetProperty(AMF_VIDEO_ENCODER_IDR_PERIOD,
                             static_cast<amf_int64>(fps * 2));

        if (codec == "hevc") {
            encoder->SetProperty(AMF_VIDEO_ENCODER_HEVC_PROFILE,
                                 AMF_VIDEO_ENCODER_HEVC_PROFILE_MAIN);
            encoder->SetProperty(AMF_VIDEO_ENCODER_HEVC_QUALITY_PRESET,
                                 AMF_VIDEO_ENCODER_HEVC_QUALITY_PRESET_SPEED);
        } else {
            encoder->SetProperty(AMF_VIDEO_ENCODER_PROFILE,
                                 AMF_VIDEO_ENCODER_PROFILE_HIGH);
            encoder->SetProperty(AMF_VIDEO_ENCODER_QUALITY_PRESET,
                                 AMF_VIDEO_ENCODER_QUALITY_PRESET_SPEED);
        }

        result = encoder->Init(AMF_SURFACE_NV12, width, height);
        if (result != AMF_OK) {
            LogAmfError("Init", result);
            return false;
        }
        return true;
    }
};

AmfEncoder::~AmfEncoder() {
    Shutdown();
}

bool AmfEncoder::Initialize(ID3D11Device* device, const std::string& codec,
                            int width, int height, int fps, int bitrate) {
    Shutdown();
    impl_ = new Impl();
    impl_->width = width;
    impl_->height = height;
    impl_->fps = fps;

    impl_->module = LoadLibraryExW(L"amfrt64.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!impl_->module) {
        impl_->module = LoadLibraryW(L"amfrt64.dll");
    }
    if (!impl_->module) {
        std::fprintf(stderr, "Failed to load amfrt64.dll\n");
        Shutdown();
        return false;
    }

    auto init = reinterpret_cast<AMFInitFn>(GetProcAddress(impl_->module, "AMFInit"));
    if (!init) {
        std::fprintf(stderr, "AMFInit not found\n");
        Shutdown();
        return false;
    }

    AMF_RESULT result = init(AMF_FULL_VERSION, &impl_->factory);
    if (result != AMF_OK || !impl_->factory) {
        LogAmfError("AMFInit", result);
        Shutdown();
        return false;
    }

    result = impl_->factory->CreateContext(&impl_->context);
    if (result != AMF_OK) {
        LogAmfError("CreateContext", result);
        Shutdown();
        return false;
    }

    result = impl_->context->InitDX11(device);
    if (result != AMF_OK) {
        LogAmfError("InitDX11", result);
        Shutdown();
        return false;
    }

    if (!impl_->CreateEncoder(codec, bitrate)) {
        Shutdown();
        return false;
    }
    return true;
}

bool AmfEncoder::Encode(ID3D11Texture2D* nv12_texture, int64_t pts_us,
                        const PacketCallback& callback) {
    if (!impl_ || !impl_->encoder || !nv12_texture) {
        return false;
    }

    amf::AMFSurfacePtr surface;
    AMF_RESULT result = impl_->context->CreateSurfaceFromDX11Native(
        nv12_texture, &surface, nullptr);
    if (result != AMF_OK || !surface) {
        LogAmfError("CreateSurfaceFromDX11Native", result);
        return false;
    }

    surface->SetPts(pts_us);
    result = impl_->encoder->SubmitInput(surface);
    if (result != AMF_OK) {
        LogAmfError("SubmitInput", result);
        return false;
    }
    return impl_->Drain(callback);
}

bool AmfEncoder::Flush(const PacketCallback& callback) {
    if (!impl_ || !impl_->encoder) {
        return false;
    }
    AMF_RESULT result = impl_->encoder->Drain();
    if (result != AMF_OK) {
        LogAmfError("Drain", result);
    }
    return impl_->Drain(callback);
}

void AmfEncoder::Shutdown() {
    if (!impl_) {
        return;
    }
    if (impl_->encoder) {
        impl_->encoder->Terminate();
        impl_->encoder = nullptr;
    }
    if (impl_->context) {
        impl_->context->Terminate();
        impl_->context = nullptr;
    }
    impl_->factory = nullptr;
    if (impl_->module) {
        FreeLibrary(impl_->module);
        impl_->module = nullptr;
    }
    delete impl_;
    impl_ = nullptr;
}

bool AmfEncoder::is_initialized() const {
    return impl_ && impl_->encoder;
}

}  // namespace hmmob
```

- [ ] **Step 7: 构建并运行 AMF 测试**

```powershell
& $cmake --preset windows-x64-release
& $cmake --build --preset windows-x64-release
ctest --test-dir build/release -C Release --output-on-failure -R AMF
```

预期：`AMF encoder produces HEVC packets` 通过；AMF 不可用时 SKIP。若编译报 AMF 宏/属性名差异，按 `third_party/amf/include` 中的实际宏名调整，不改接口。

- [ ] **Step 8: 提交**

```powershell
git add scripts/fetch_amf.ps1 src/core/encoder/AmfEncoder.* tests/test_amf_encoder.cpp CMakeLists.txt
git commit -m "feat: add amd amf hevc/h264 encoder"
```

---

## Task 8: FFmpeg MP4 封装

**Files:**
- Create: `src/core/mux/FfmpegMuxer.h/.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: 写 `src/core/mux/FfmpegMuxer.h`**

```cpp
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

namespace hmmob {

class FfmpegMuxer {
public:
    FfmpegMuxer() = default;
    ~FfmpegMuxer();
    FfmpegMuxer(const FfmpegMuxer&) = delete;
    FfmpegMuxer& operator=(const FfmpegMuxer&) = delete;

    bool Open(const std::string& path, const std::string& codec,
              int width, int height, int fps, int bitrate);
    bool WritePacket(const uint8_t* data, size_t size, int64_t pts_us, bool keyframe);
    bool Close();

private:
    struct Impl;
    Impl* impl_ = nullptr;
};

}  // namespace hmmob
```

- [ ] **Step 2: 写 `src/core/mux/FfmpegMuxer.cpp`**

```cpp
#include "core/mux/FfmpegMuxer.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
}

#include <cstdio>
#include <cstring>

namespace hmmob {

struct FfmpegMuxer::Impl {
    AVFormatContext* format = nullptr;
    AVStream* stream = nullptr;
    bool header_written = false;
};

FfmpegMuxer::~FfmpegMuxer() {
    Close();
}

bool FfmpegMuxer::Open(const std::string& path, const std::string& codec,
                       int width, int height, int fps, int bitrate) {
    Close();
    impl_ = new Impl();

    if (avformat_alloc_output_context2(&impl_->format, nullptr, "mp4", path.c_str()) < 0 ||
        !impl_->format) {
        std::fprintf(stderr, "avformat_alloc_output_context2 failed\n");
        Close();
        return false;
    }

    impl_->stream = avformat_new_stream(impl_->format, nullptr);
    if (!impl_->stream) {
        std::fprintf(stderr, "avformat_new_stream failed\n");
        Close();
        return false;
    }

    AVCodecParameters* par = impl_->stream->codecpar;
    par->codec_type = AVMEDIA_TYPE_VIDEO;
    par->codec_id = codec == "hevc" ? AV_CODEC_ID_HEVC : AV_CODEC_ID_H264;
    par->width = width;
    par->height = height;
    par->format = AV_PIX_FMT_YUV420P;
    par->bit_rate = bitrate;

    impl_->stream->time_base = AVRational{1, 1000000};
    impl_->stream->avg_frame_rate = AVRational{fps, 1};
    impl_->stream->r_frame_rate = AVRational{fps, 1};

    if (avio_open(&impl_->format->pb, path.c_str(), AVIO_FLAG_WRITE) < 0) {
        std::fprintf(stderr, "avio_open failed: %s\n", path.c_str());
        Close();
        return false;
    }

    AVDictionary* options = nullptr;
    av_dict_set(&options, "movflags", "+faststart", 0);
    int result = avformat_write_header(impl_->format, &options);
    av_dict_free(&options);
    if (result < 0) {
        std::fprintf(stderr, "avformat_write_header failed: %d\n", result);
        Close();
        return false;
    }

    impl_->header_written = true;
    return true;
}

bool FfmpegMuxer::WritePacket(const uint8_t* data, size_t size,
                              int64_t pts_us, bool keyframe) {
    if (!impl_ || !impl_->format || !impl_->stream || !data || size == 0) {
        return false;
    }

    AVPacket* packet = av_packet_alloc();
    if (!packet) {
        return false;
    }
    if (av_new_packet(packet, static_cast<int>(size)) < 0) {
        av_packet_free(&packet);
        return false;
    }

    std::memcpy(packet->data, data, size);
    packet->pts = pts_us;
    packet->dts = pts_us;
    packet->stream_index = impl_->stream->index;
    packet->flags = keyframe ? AV_PKT_FLAG_KEY : 0;
    av_packet_rescale_ts(packet, AVRational{1, 1000000}, impl_->stream->time_base);

    int result = av_interleaved_write_frame(impl_->format, packet);
    av_packet_free(&packet);
    return result >= 0;
}

bool FfmpegMuxer::Close() {
    if (!impl_) {
        return true;
    }
    bool ok = true;
    if (impl_->format) {
        if (impl_->header_written) {
            ok = av_write_trailer(impl_->format) >= 0;
        }
        if (impl_->format->pb) {
            avio_closep(&impl_->format->pb);
        }
        avformat_free_context(impl_->format);
    }
    delete impl_;
    impl_ = nullptr;
    return ok;
}

}  // namespace hmmob
```

- [ ] **Step 3: 修改 `CMakeLists.txt`**

```cmake
add_library(hmmob_core STATIC
  ...
  src/core/mux/FfmpegMuxer.cpp
)
```

- [ ] **Step 4: 构建**

```powershell
& $cmake --preset windows-x64-release
& $cmake --build --preset windows-x64-release
```

预期：编译通过。封装器由 Task 9 的端到端测试覆盖。

- [ ] **Step 5: 提交**

```powershell
git add src/core/mux/FfmpegMuxer.* CMakeLists.txt
git commit -m "feat: add ffmpeg mp4 muxer"
```

---

## Task 9: evspike 端到端 4K60 SDR 录制

**Files:**
- Modify: `src/spike/main.cpp`

- [ ] **Step 1: 替换 `src/spike/main.cpp`**

```cpp
#include <cstdio>
#include <cstdlib>
#include <string>

#include "core/capture/DxgiCapture.h"
#include "core/common/QpcClock.h"
#include "core/encoder/AmfEncoder.h"
#include "core/gfx/D3D11Device.h"
#include "core/gfx/VideoProcessor.h"
#include "core/mux/FfmpegMuxer.h"

namespace {

struct Options {
    std::string output = "out.mp4";
    int seconds = 10;
    int fps = 60;
    std::string codec = "hevc";
    int display = 0;
    int bitrate = 50000000;
    bool hdr = false;
};

bool Parse(int argc, char** argv, Options& options) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto next = [&](const char* name) -> const char* {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "missing value for %s\n", name);
                return nullptr;
            }
            return argv[++i];
        };
        if (arg == "--output") {
            const char* v = next("--output"); if (!v) return false; options.output = v;
        } else if (arg == "--seconds") {
            const char* v = next("--seconds"); if (!v) return false; options.seconds = std::atoi(v);
        } else if (arg == "--fps") {
            const char* v = next("--fps"); if (!v) return false; options.fps = std::atoi(v);
        } else if (arg == "--codec") {
            const char* v = next("--codec"); if (!v) return false; options.codec = v;
        } else if (arg == "--display") {
            const char* v = next("--display"); if (!v) return false; options.display = std::atoi(v);
        } else if (arg == "--bitrate") {
            const char* v = next("--bitrate"); if (!v) return false; options.bitrate = std::atoi(v);
        } else if (arg == "--hdr") {
            options.hdr = true;
        } else {
            std::fprintf(stderr, "unknown option: %s\n", arg.c_str());
            return false;
        }
    }
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    Options options;
    if (!Parse(argc, argv, options)) {
        return 1;
    }
    if (options.codec != "hevc" && options.codec != "h264") {
        std::fprintf(stderr, "codec must be hevc or h264\n");
        return 1;
    }

    hmmob::D3D11Device d3d;
    if (!d3d.Create()) {
        std::fprintf(stderr, "failed to create d3d11 device\n");
        return 1;
    }

    const auto displays = d3d.EnumerateDisplays();
    if (displays.empty()) {
        std::fprintf(stderr, "no display found\n");
        return 1;
    }
    if (options.display < 0 || options.display >= static_cast<int>(displays.size())) {
        std::fprintf(stderr, "display index out of range\n");
        return 1;
    }

    hmmob::DxgiCapture capture;
    if (!capture.Initialize(d3d, options.display, options.hdr)) {
        std::fprintf(stderr, "failed to initialize dxgi capture\n");
        return 1;
    }

    if (capture.hdr()) {
        std::fprintf(stderr, "HDR capture not implemented in this task; run without --hdr\n");
        return 2;
    }

    const int width = capture.width();
    const int height = capture.height();
    std::printf("display=%d %dx%d format=%d fps=%d codec=%s\n",
                options.display, width, height,
                static_cast<int>(capture.format()), options.fps, options.codec.c_str());

    hmmob::VideoProcessor converter;
    if (!converter.Initialize(d3d, width, height)) {
        std::fprintf(stderr, "failed to initialize video processor\n");
        return 1;
    }

    hmmob::AmfEncoder encoder;
    if (!encoder.Initialize(d3d.device(), options.codec, width, height,
                            options.fps, options.bitrate)) {
        std::fprintf(stderr, "failed to initialize AMF encoder\n");
        return 1;
    }

    hmmob::FfmpegMuxer muxer;
    if (!muxer.Open(options.output, options.codec, width, height,
                    options.fps, options.bitrate)) {
        std::fprintf(stderr, "failed to open output %s\n", options.output.c_str());
        return 1;
    }

    hmmob::QpcClock clock;
    const int64_t start_us = clock.NowUs();
    int captured = 0;
    int encoded_packets = 0;
    int64_t encoded_bytes = 0;

    while (clock.NowUs() - start_us < static_cast<int64_t>(options.seconds) * 1000000) {
        hmmob::CaptureFrame frame;
        if (!capture.Acquire(frame, 100)) {
            if (capture.access_lost()) {
                std::fprintf(stderr, "capture access lost\n");
                break;
            }
            continue;
        }
        ++captured;

        if (!converter.Convert(frame.texture.Get(), converter.output_texture())) {
            std::fprintf(stderr, "color conversion failed\n");
            break;
        }

        const bool ok = encoder.Encode(converter.output_texture(), frame.pts_us,
            [&](const hmmob::AmfPacket& packet) {
                ++encoded_packets;
                encoded_bytes += static_cast<int64_t>(packet.data.size());
                muxer.WritePacket(packet.data.data(), packet.data.size(),
                                  packet.pts_us, packet.keyframe);
            });
        if (!ok) {
            std::fprintf(stderr, "encode failed\n");
            break;
        }
    }

    encoder.Flush([&](const hmmob::AmfPacket& packet) {
        ++encoded_packets;
        encoded_bytes += static_cast<int64_t>(packet.data.size());
        muxer.WritePacket(packet.data.data(), packet.data.size(),
                          packet.pts_us, packet.keyframe);
    });

    muxer.Close();

    const double elapsed_s = static_cast<double>(clock.NowUs() - start_us) / 1000000.0;
    std::printf("captured=%d packets=%d bytes=%lld elapsed=%.2fs avg_fps=%.2f\n",
                captured, encoded_packets, static_cast<long long>(encoded_bytes),
                elapsed_s, captured / elapsed_s);
    return 0;
}
```

- [ ] **Step 2: 构建**

```powershell
& $cmake --preset windows-x64-release
& $cmake --build --preset windows-x64-release
```

- [ ] **Step 3: 运行 10 秒 4K60 录制**

```powershell
Set-Location C:\Users\Administrator\Desktop\project\hmmob-ev
.\build\release\Release\evspike.exe --output C:\temp\hmmob-phase0.mp4 --seconds 10 --fps 60 --codec hevc --bitrate 50000000
```

预期：`captured≈600`，`avg_fps≈60`，生成 MP4。

- [ ] **Step 4: 用 ffprobe 校验**

```powershell
C:\vcpkg\installed\x64-windows\tools\ffmpeg\ffprobe.exe -v error -show_entries stream=codec_name,width,height,avg_frame_rate -show_entries format=duration,size -of default=noprint_wrappers=1 C:\temp\hmmob-phase0.mp4
```

预期：`codec_name=hevc`，分辨率 3840x2160（或实际显示器分辨率），`avg_frame_rate=60/1`，`duration≈10`。

- [ ] **Step 5: 提交**

```powershell
git add src/spike/main.cpp CMakeLists.txt
git commit -m "feat: add end-to-end 4k60 capture and amf encode spike"
```

---

## Task 10: HDR 抓取与基础色调映射

**Files:**
- Create: `shaders/hdr_to_sdr.hlsl`, `src/core/gfx/HdrToneMapper.h/.cpp`
- Modify: `src/spike/main.cpp`, `CMakeLists.txt`

- [ ] **Step 1: 写 `shaders/hdr_to_sdr.hlsl`**

```hlsl
Texture2D<float4> src : register(t0);
SamplerState samp : register(s0);

cbuffer Params : register(b0) {
    float sdr_white_nits;
    float max_nits;
    int pq_mode;
    int pad0;
};

struct VSOut { float4 pos : SV_POSITION; float2 uv : TEXCOORD0; };

VSOut VSMain(uint id : SV_VertexID) {
    float2 p = float2((id << 1) & 2, id & 2);
    VSOut o;
    o.pos = float4(p * float2(2, -2) + float2(-1, 1), 0, 1);
    o.uv = p;
    return o;
}

float3 PqToLinear(float3 pq) {
    const float m1 = 0.1593017578125;
    const float m2 = 78.84375;
    const float c1 = 0.8359375;
    const float c2 = 18.8515625;
    const float c3 = 18.6875;
    float3 p = pow(max(pq, 0.0), 1.0 / m2);
    float3 num = max(p - c1, 0.0);
    float3 den = c2 - c3 * p;
    return pow(num / den, 1.0 / m1) * 10000.0;
}

float3 LinearToSrgb(float3 c) {
    c = max(c, 0.0);
    float3 low = c * 12.92;
    float3 high = 1.055 * pow(c, 1.0 / 2.4) - 0.055;
    return lerp(high, low, step(c, 0.0031308));
}

float4 PSMain(VSOut input) : SV_TARGET {
    float4 sample = src.Sample(samp, input.uv);
    float3 nits = pq_mode ? PqToLinear(sample.rgb) : sample.rgb * 80.0;
    const float3x3 m = float3x3(
        1.6605, -0.1246, -0.0182,
       -0.5876,  1.1329, -0.1006,
       -0.0728, -0.0083,  1.1187);
    float3 lin = mul(m, nits);
    lin /= max(sdr_white_nits, 1.0);
    lin = lin / (1.0 + lin * (1.0 / max(max_nits, 1.0)));
    return float4(LinearToSrgb(lin), 1.0);
}
```

- [ ] **Step 2: 写 `src/core/gfx/HdrToneMapper.h`**

```cpp
#pragma once
#include "core/gfx/D3D11Device.h"
#include <d3d11.h>
#include <wrl/client.h>

namespace hmmob {

class HdrToneMapper {
public:
    bool Initialize(D3D11Device& d3d, int width, int height, bool pq_mode);
    ID3D11Texture2D* Map(ID3D11Texture2D* hdr_texture);
    ID3D11Texture2D* output_texture() const { return output_.Get(); }

private:
    ID3D11Device* device_ = nullptr;
    ID3D11DeviceContext* context_ = nullptr;
    Microsoft::WRL::ComPtr<ID3D11VertexShader> vertex_shader_;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> pixel_shader_;
    Microsoft::WRL::ComPtr<ID3D11Buffer> constants_;
    Microsoft::WRL::ComPtr<ID3D11SamplerState> sampler_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> output_;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> output_rtv_;
    int width_ = 0;
    int height_ = 0;
    bool pq_mode_ = true;
};

}  // namespace hmmob
```

- [ ] **Step 3: 写 `src/core/gfx/HdrToneMapper.cpp`**

```cpp
#include "core/gfx/HdrToneMapper.h"
#include "core/common/Check.h"
#include <d3dcompiler.h>
#include <cstdio>
#include <cstring>

namespace hmmob {

namespace {

const char* kShaderSource = R"(
Texture2D<float4> src : register(t0);
SamplerState samp : register(s0);
cbuffer Params : register(b0) {
    float sdr_white_nits;
    float max_nits;
    int pq_mode;
    int pad0;
};
struct VSOut { float4 pos : SV_POSITION; float2 uv : TEXCOORD0; };
VSOut VSMain(uint id : SV_VertexID) {
    float2 p = float2((id << 1) & 2, id & 2);
    VSOut o;
    o.pos = float4(p * float2(2, -2) + float2(-1, 1), 0, 1);
    o.uv = p;
    return o;
}
float3 PqToLinear(float3 pq) {
    const float m1 = 0.1593017578125;
    const float m2 = 78.84375;
    const float c1 = 0.8359375;
    const float c2 = 18.8515625;
    const float c3 = 18.6875;
    float3 p = pow(max(pq, 0.0), 1.0 / m2);
    float3 num = max(p - c1, 0.0);
    float3 den = c2 - c3 * p;
    return pow(num / den, 1.0 / m1) * 10000.0;
}
float3 LinearToSrgb(float3 c) {
    c = max(c, 0.0);
    float3 low = c * 12.92;
    float3 high = 1.055 * pow(c, 1.0 / 2.4) - 0.055;
    return lerp(high, low, step(c, 0.0031308));
}
float4 PSMain(VSOut input) : SV_TARGET {
    float4 sample = src.Sample(samp, input.uv);
    float3 nits = pq_mode ? PqToLinear(sample.rgb) : sample.rgb * 80.0;
    const float3x3 m = float3x3(
        1.6605, -0.1246, -0.0182,
       -0.5876,  1.1329, -0.1006,
       -0.0728, -0.0083,  1.1187);
    float3 lin = mul(m, nits);
    lin /= max(sdr_white_nits, 1.0);
    lin = lin / (1.0 + lin * (1.0 / max(max_nits, 1.0)));
    return float4(LinearToSrgb(lin), 1.0);
}
)";

struct Constants {
    float sdr_white_nits;
    float max_nits;
    int pq_mode;
    int pad0;
};

}  // namespace

bool HdrToneMapper::Initialize(D3D11Device& d3d, int width, int height, bool pq_mode) {
    device_ = d3d.device();
    context_ = d3d.context();
    width_ = width;
    height_ = height;
    pq_mode_ = pq_mode;

    Microsoft::WRL::ComPtr<ID3DBlob> vs_blob;
    Microsoft::WRL::ComPtr<ID3DBlob> ps_blob;
    Microsoft::WRL::ComPtr<ID3DBlob> error;
    HRESULT hr = D3DCompile(kShaderSource, std::strlen(kShaderSource), nullptr, nullptr,
                            nullptr, "VSMain", "vs_5_0", 0, 0, &vs_blob, &error);
    if (FAILED(hr)) {
        std::fprintf(stderr, "VS compile failed: %s\n",
                     error ? static_cast<const char*>(error->GetBufferPointer()) : "");
        return false;
    }
    hr = D3DCompile(kShaderSource, std::strlen(kShaderSource), nullptr, nullptr,
                    nullptr, "PSMain", "ps_5_0", 0, 0, &ps_blob, &error);
    if (FAILED(hr)) {
        std::fprintf(stderr, "PS compile failed: %s\n",
                     error ? static_cast<const char*>(error->GetBufferPointer()) : "");
        return false;
    }

    HMMOB_HR_CHECK(device_->CreateVertexShader(vs_blob->GetBufferPointer(),
                                               vs_blob->GetBufferSize(), nullptr, &vertex_shader_));
    HMMOB_HR_CHECK(device_->CreatePixelShader(ps_blob->GetBufferPointer(),
                                              ps_blob->GetBufferSize(), nullptr, &pixel_shader_));

    D3D11_BUFFER_DESC cb_desc{};
    cb_desc.ByteWidth = sizeof(Constants);
    cb_desc.Usage = D3D11_USAGE_DEFAULT;
    cb_desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    HMMOB_HR_CHECK(device_->CreateBuffer(&cb_desc, nullptr, &constants_));

    D3D11_SAMPLER_DESC sampler_desc{};
    sampler_desc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampler_desc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler_desc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler_desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    HMMOB_HR_CHECK(device_->CreateSamplerState(&sampler_desc, &sampler_));

    D3D11_TEXTURE2D_DESC out_desc{};
    out_desc.Width = static_cast<UINT>(width);
    out_desc.Height = static_cast<UINT>(height);
    out_desc.MipLevels = 1;
    out_desc.ArraySize = 1;
    out_desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    out_desc.SampleDesc.Count = 1;
    out_desc.Usage = D3D11_USAGE_DEFAULT;
    out_desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    HMMOB_HR_CHECK(device_->CreateTexture2D(&out_desc, nullptr, &output_));
    HMMOB_HR_CHECK(device_->CreateRenderTargetView(output_.Get(), nullptr, &output_rtv_));
    return true;
}

ID3D11Texture2D* HdrToneMapper::Map(ID3D11Texture2D* hdr_texture) {
    if (!hdr_texture || !output_rtv_) {
        return nullptr;
    }

    D3D11_TEXTURE2D_DESC desc{};
    hdr_texture->GetDesc(&desc);

    D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc{};
    srv_desc.Format = desc.Format;
    srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srv_desc.Texture2D.MipLevels = 1;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv;
    if (FAILED(device_->CreateShaderResourceView(hdr_texture, &srv_desc, &srv))) {
        return nullptr;
    }

    Constants constants{203.0f, 1000.0f, pq_mode_ ? 1 : 0, 0};
    context_->UpdateSubresource(constants_.Get(), 0, nullptr, &constants, 0, 0);

    D3D11_VIEWPORT viewport{};
    viewport.Width = static_cast<float>(width_);
    viewport.Height = static_cast<float>(height_);
    viewport.MaxDepth = 1.0f;
    context_->RSSetViewports(1, &viewport);

    ID3D11RenderTargetView* rtv = output_rtv_.Get();
    context_->OMSetRenderTargets(1, &rtv, nullptr);
    ID3D11ShaderResourceView* srvs[] = {srv.Get()};
    context_->PSSetShaderResources(0, 1, srvs);
    ID3D11SamplerState* samplers[] = {sampler_.Get()};
    context_->PSSetSamplers(0, 1, samplers);
    ID3D11Buffer* cbs[] = {constants_.Get()};
    context_->PSSetConstantBuffers(0, 1, cbs);
    context_->VSSetShader(vertex_shader_.Get(), nullptr, 0);
    context_->PSSetShader(pixel_shader_.Get(), nullptr, 0);
    context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context_->Draw(3, 0);
    return output_.Get();
}

}  // namespace hmmob
```

- [ ] **Step 4: 修改 `CMakeLists.txt`**

```cmake
add_library(hmmob_core STATIC
  ...
  src/core/gfx/HdrToneMapper.cpp
)
target_link_libraries(hmmob_core PUBLIC
  ...
  d3d11 dxgi d3dcompiler)
```

- [ ] **Step 5: 修改 `src/spike/main.cpp`**

在 `main` 中，捕获初始化之后加入：

```cpp
hmmob::HdrToneMapper tone_mapper;
const bool pq_mode = capture.format() == DXGI_FORMAT_R10G10B10A2_UNORM;
if (capture.hdr() && !tone_mapper.Initialize(d3d, width, height, pq_mode)) {
    std::fprintf(stderr, "failed to initialize HDR tone mapper\n");
    return 1;
}
```

删除 Task 9 中的：

```cpp
if (capture.hdr()) {
    std::fprintf(stderr, "HDR capture not implemented in this task; run without --hdr\n");
    return 2;
}
```

把转换输入改为：

```cpp
ID3D11Texture2D* source = frame.texture.Get();
if (frame.hdr) {
    source = tone_mapper.Map(frame.texture.Get());
    if (!source) {
        std::fprintf(stderr, "HDR tone mapping failed\n");
        break;
    }
}
if (!converter.Convert(source, converter.output_texture())) {
    std::fprintf(stderr, "color conversion failed\n");
    break;
}
```

- [ ] **Step 6: 构建并在 HDR 显示器上测试**

```powershell
& $cmake --preset windows-x64-release
& $cmake --build --preset windows-x64-release
.\build\release\Release\evspike.exe --output C:\temp\hmmob-hdr.mp4 --seconds 10 --fps 60 --codec hevc --hdr
```

预期：程序不报错，MP4 可播放；与关闭 HDR 的 SDR 录屏相比，亮部不过曝、颜色无明显偏色。若本机没有 HDR 显示器，记录“未验证”，到有 HDR 显示器的机器补测。

- [ ] **Step 7: 提交**

```powershell
git add shaders/hdr_to_sdr.hlsl src/core/gfx/HdrToneMapper.* src/spike/main.cpp CMakeLists.txt
git commit -m "feat: add hdr capture tone mapping spike"
```

---

## Task 11: 10 分钟稳定性验证与 Phase 0 报告

**Files:**
- Create: `docs/phase0-report.md`
- Modify: `README.md`

- [ ] **Step 1: 运行 10 分钟 4K60 录制**

```powershell
Set-Location C:\Users\Administrator\Desktop\project\hmmob-ev
.\build\release\Release\evspike.exe --output C:\temp\hmmob-10min.mp4 --seconds 600 --fps 60 --codec hevc --bitrate 50000000
```

- [ ] **Step 2: 记录结果**

写入 `docs/phase0-report.md`：

```markdown
# Phase 0 验证报告

- 日期：
- 机器：
- GPU：
- 驱动版本：
- 显示器 / 分辨率 / HDR：
- 编码器：AMF HEVC
- 分辨率 / FPS / 码率：
- 录制时长：
- captured / packets / avg_fps：
- 输出文件大小：
- ffprobe 结果：
- 观察到的丢帧/卡顿：
- CPU / GPU 占用：
- 结论：通过 / 不通过
- 后续问题：
```

- [ ] **Step 3: 用 ffprobe 校验**

```powershell
C:\vcpkg\installed\x64-windows\tools\ffmpeg\ffprobe.exe -v error -show_entries stream=codec_name,width,height,avg_frame_rate -show_entries format=duration,size -of default=noprint_wrappers=1 C:\temp\hmmob-10min.mp4
```

预期：`duration≈600`，`avg_frame_rate=60/1`，分辨率正确。

- [ ] **Step 4: 更新 `README.md`**

加入：

```markdown
Phase 0 已完成：DXGI 4K60 抓屏、D3D11 BGRA→NV12、AMD AMF HEVC/H.264 编码、FFmpeg MP4 封装。
详细结果见 `docs/phase0-report.md`。
```

- [ ] **Step 5: 提交**

```powershell
git add docs/phase0-report.md README.md
git commit -m "docs: add phase 0 validation report"
```

---

## 4. 自审检查

- **Spec 覆盖**：Phase 0 覆盖 DXGI 采集、D3D11 颜色转换、AMF 编码、FFmpeg 封装、HDR 抓取/色调映射、4K60 性能验证。Qt UI、摄像头、音频、叠加层、RTMP、编辑器属于后续计划。
- **占位符**：无 TBD/TODO；每个代码步骤给出完整文件内容。
- **类型一致性**：`CaptureFrame`、`AmfPacket`、`D3D11Device`、`VideoProcessor`、`AmfEncoder`、`FfmpegMuxer` 的命名在任务间保持一致。
- **环境限制**：本机只有 AMD GPU，AMF 测试可用；NVENC/QSV 在后续机器验证。HDR 测试需要 HDR 显示器，没有则记录未验证。

## 5. 执行方式

Plan complete and saved to `docs/superpowers/plans/2026-11-07-phase0-capture-encode-spike.md`.

两种执行方式：

1. **Subagent-Driven（推荐）**：每个 Task 派一个子代理执行，我在任务之间做 review，迭代快。
2. **Inline Execution**：在当前会话按批次执行，每批后设检查点。
