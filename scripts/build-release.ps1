# Build both release artifacts:
#   dist\Hmmob-EV-<version>-win64.zip           portable ("green") build
#   dist\Hmmob-EV-<version>-win64-setup.exe     per-user installer (Inno Setup)
# Usage: powershell -ExecutionPolicy Bypass -File .\scripts\build-release.ps1 [-Version 0.1.1]
param(
    [string]$Version = "0.1.1"
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot

& (Join-Path $PSScriptRoot "package.ps1") -Version $Version

$candidates = @(
    (Join-Path $env:LOCALAPPDATA "InnoSetup6\ISCC.exe"),
    "C:\Program Files (x86)\Inno Setup 6\ISCC.exe",
    "C:\Program Files\Inno Setup 6\ISCC.exe"
)
$iscc = $candidates | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $iscc) {
    Write-Warning "Inno Setup 6 (ISCC.exe) not found; installer skipped. Portable zip is ready."
    exit 0
}

& $iscc "/DMyAppVersion=$Version" (Join-Path $PSScriptRoot "installer.iss")
if ($LASTEXITCODE -ne 0) {
    throw "Inno Setup compilation failed with exit code $LASTEXITCODE"
}

$installer = Join-Path $root "dist\Hmmob-EV-$Version-win64-setup.exe"
if (-not (Test-Path $installer)) {
    throw "Installer was not produced at $installer"
}
"Installer: $installer ($([math]::Round((Get-Item $installer).Length / 1MB, 1)) MiB)"
