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
