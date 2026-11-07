$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$dest = Join-Path $root "third_party\amf"
$include = Join-Path $dest "include\components\VideoEncoderHEVC.h"
# Pinned so a fetched header set is reproducible. This is the AMF v1.5.3 tag.
$amfRef = "v1.5.3"

if (Test-Path $include) {
    Write-Host "AMF headers already present"
    exit 0
}

New-Item -ItemType Directory -Force -Path $dest | Out-Null
$src = Join-Path $dest "src"
if (Test-Path $src) { Remove-Item $src -Recurse -Force }
# Remove any partial install so Copy-Item cannot create include\include.
$install = Join-Path $dest "include"
if (Test-Path $install) { Remove-Item $install -Recurse -Force }
git clone --depth 1 --branch $amfRef https://github.com/GPUOpen-LibrariesAndSDKs/AMF.git $src
Copy-Item (Join-Path $src "amf\public\include") $install -Recurse
Remove-Item $src -Recurse -Force
$version = Select-String -Path (Join-Path $install "core\Version.h") `
    -Pattern "#define AMF_VERSION_(MAJOR|MINOR|RELEASE|BUILD_NUM)\s+(\d+)" |
    ForEach-Object { $_.Matches[0].Groups[2].Value }
Write-Host "AMF $($version -join '.') headers installed to $install"
