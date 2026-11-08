# Third-Party Notices

Hmmob EV is a clean-room implementation. It dynamically links against the
following third-party components:

## Qt 6 (Widgets, Gui, Core)

- Version: 6.11.3
- License: GNU Lesser General Public License v3 (LGPL-3.0)
- Usage: dynamically linked (`Qt6Core.dll`, `Qt6Gui.dll`, `Qt6Widgets.dll`).
  The DLLs in this package may be replaced with compatible builds of Qt 6.

## FFmpeg

- Version: 9.0.2 (LGPL build)
- License: GNU Lesser General Public License v2.1 or later (LGPL-2.1+)
- Usage: dynamically linked (`avcodec-63.dll`, `avformat-63.dll`,
  `avutil-61.dll`, `swresample-7.dll`, `swscale-10.dll`).

## AMD AMF

- Version: 1.5.3 headers
- License: MIT
- Usage: headers only; the runtime is provided by the installed AMD driver.

## Catch2

- Version: 3.16.1
- License: Boost Software License 1.0
- Usage: tests only; not included in this package.

## Other runtime libraries

`double-conversion`, `md4c`, `pcre2`, `zlib` are Qt/FFmpeg dependencies and are
distributed under their respective permissive licenses (BSD/MIT/zlib).

Microsoft Visual C++ runtime DLLs (`msvcp140.dll`, `vcruntime140.dll`,
`vcruntime140_1.dll`) are redistributed under the Microsoft Visual Studio
redistributable terms.

Windows Graphics Capture, DXGI Desktop Duplication, WASAPI and Media
Foundation are operating-system APIs and are not redistributed.
