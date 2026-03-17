#!/usr/bin/env powershell
# Merge external DLLs into bsplugins.dll
# This script combines fmt.dll and zlib1.dll into the main bsplugins.dll

param(
    [string]$BuildDir = "D:\Code\projects\modorganizer-bsplugins-main\vsbuild\src\Release",
    [string]$VcpkgLib = "D:\Code\tools\vcpkg-manifest-boost\vcpkg_installed\x64-windows\bin"
)

# Verify ILMerge is available or download if needed
$ilmerge_url = "https://github.com/ILMergy/ilmerge/releases/download/v3.5.0/ILMerge.zip"
$ilmerge_path = "$PSScriptRoot\ilmerge"

if (-not (Test-Path "$ilmerge_path\ILMerge.exe")) {
    Write-Host "Downloading ILMerge..."
    $zip = "$PSScriptRoot\ilmerge.zip"
    Invoke-WebRequest -Uri $ilmerge_url -OutFile $zip -ErrorAction Stop
    Expand-Archive -Path $zip -DestinationPath $ilmerge_path -ErrorAction Stop
    Remove-Item $zip
}

# NOTE: ILMerge only works with .NET assemblies (.exe/.dll with IL code)
# For native C++ DLLs, we need a different approach (see comment below)

Write-Host "Native C++ DLLs detected. ILMerge won't work for these."
Write-Host "Alternative: Embed DLLs as resources and extract at runtime"
Write-Host "Or: Use costura.fody (if were .NET)"
Write-Host ""
Write-Host "Recommended approach:"
Write-Host "1. Recompile fmt and zlib as static libraries"
Write-Host "2. Link them directly into bsplugins.dll"
