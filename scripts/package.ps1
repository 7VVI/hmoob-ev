# Build a self-contained Windows x64 release package.
# Usage: powershell -ExecutionPolicy Bypass -File .\scripts\package.ps1 [-Version 0.1.0]
param(
    [string]$Version = "0.1.0"
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$release = Join-Path $root "build\release\Release"
$distRoot = Join-Path $root "dist"
$name = "Hmmob-EV-$Version-win64"
$out = Join-Path $distRoot $name
$zip = Join-Path $distRoot "$name.zip"

if (-not (Test-Path (Join-Path $release "hmmob_ev_app.exe"))) {
    throw "Release build not found at $release. Run cmake --build --preset windows-x64-release first."
}

if (Test-Path $out) { Remove-Item -Recurse -Force $out }
New-Item -ItemType Directory -Path $out | Out-Null

# Executable and its Qt/FFmpeg dependencies (vcpkg applocal copies these).
Copy-Item (Join-Path $release "hmmob_ev_app.exe") $out
$dlls = @(
    "Qt6Core.dll", "Qt6Gui.dll", "Qt6Widgets.dll",
    "avcodec-63.dll", "avformat-63.dll", "avutil-61.dll",
    "swresample-7.dll", "swscale-10.dll",
    "double-conversion.dll", "md4c.dll", "pcre2-16.dll", "z.dll"
)
foreach ($dll in $dlls) {
    $source = Join-Path $release $dll
    if (-not (Test-Path $source)) { throw "Missing runtime DLL: $dll" }
    Copy-Item $source $out
}

# Qt platform and style plugins.
$plugins = Join-Path $out "platforms"
New-Item -ItemType Directory -Path $plugins | Out-Null
foreach ($plugin in @("qwindows.dll", "qdirect2d.dll", "qminimal.dll")) {
    Copy-Item (Join-Path $release "platforms\$plugin") $plugins
}
$styles = Join-Path $out "styles"
New-Item -ItemType Directory -Path $styles | Out-Null
Copy-Item (Join-Path $release "styles\qmodernwindowsstyle.dll") $styles

# Microsoft Visual C++ runtime (redistributable).
foreach ($dll in @("msvcp140.dll", "vcruntime140.dll", "vcruntime140_1.dll")) {
    Copy-Item (Join-Path $env:SystemRoot "System32\$dll") $out
}

# Documentation and design assets.
Copy-Item (Join-Path $root "README.md") $out
Copy-Item (Join-Path $root "THIRD_PARTY_NOTICES.md") $out
$assets = Join-Path $out "assets"
New-Item -ItemType Directory -Path $assets | Out-Null
Copy-Item (Join-Path $root "assets\logo-256.png") $assets
Copy-Item (Join-Path $root "assets\logo.svg") $assets

if (Test-Path $zip) { Remove-Item -Force $zip }
Compress-Archive -Path $out -DestinationPath $zip -CompressionLevel Optimal

$files = Get-ChildItem -Recurse $out | Where-Object { -not $_.PSIsContainer }
"Package: $out"
"Zip:     $zip"
"Files:   $($files.Count), $([math]::Round((Get-Item $zip).Length / 1MB, 1)) MiB"
