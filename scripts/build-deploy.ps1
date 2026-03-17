param(
  [string]$Configuration = "Release",
  [string]$Preset = "windows-2022",
  [string]$DestinationRoot = "F:\Games\Wunduniik Modlist",
  [string]$CMakeExe = "cmake",
  [switch]$SkipDeploy
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
Set-Location $repoRoot

if (-not (Test-Path "vsbuild")) {
  & $CMakeExe --preset $Preset
}

& $CMakeExe --build vsbuild --config $Configuration --target bsplugins

$dllPath = Join-Path $repoRoot "vsbuild\src\$Configuration\bsplugins.dll"
if (-not (Test-Path $dllPath)) {
  throw "Build completed but bsplugins.dll was not found at '$dllPath'."
}

$dumpbin = (Get-Command dumpbin.exe -ErrorAction SilentlyContinue).Source
if (-not $dumpbin) {
  $fallback = "D:\Code\tools\BuildTools\VC\Tools\MSVC\14.44.35207\bin\Hostx64\x64\dumpbin.exe"
  if (Test-Path $fallback) {
    $dumpbin = $fallback
  } else {
    throw "dumpbin.exe not found. Install Visual Studio Build Tools or adjust script path."
  }
}

$dependentsOutput = & $dumpbin /DEPENDENTS $dllPath
$dependents = @($dependentsOutput |
  ForEach-Object { $_.ToString().Trim() } |
  Where-Object { $_ -match '^[A-Za-z0-9._-]+\.dll$' })

$forbidden = @("fmt.dll", "zlib1.dll")
$forbiddenFound = $dependents | Where-Object {
  $forbidden -contains $_.ToLowerInvariant()
}
if ($forbiddenFound.Count -gt 0) {
  throw "Forbidden runtime dependencies detected: $($forbiddenFound -join ', ')"
}

$destinationDll = ""
if (-not $SkipDeploy) {
  $destinationPluginDir = Join-Path $DestinationRoot "plugins"
  if (-not (Test-Path $destinationPluginDir)) {
    throw "Destination plugin folder not found: '$destinationPluginDir'"
  }

  $destinationDll = Join-Path $destinationPluginDir "bsplugins.dll"
  try {
    Copy-Item $dllPath $destinationDll -Force
  } catch {
    throw "Failed to deploy '$destinationDll'. Close Mod Organizer (or anything using the DLL) and retry. Original error: $($_.Exception.Message)"
  }
}

$hash = (Get-FileHash $dllPath -Algorithm SHA256).Hash
$timestamp = (Get-Date).ToString("yyyy-MM-dd HH:mm:ss zzz")
$reportPath = Join-Path $repoRoot "vsbuild\src\$Configuration\build-report.txt"
$report = @(
  "timestamp=$timestamp",
  "dll=$dllPath",
  "deployed=$destinationDll",
  "sha256=$hash",
  "dependencies=$($dependents -join ';')"
)
$report | Set-Content -Path $reportPath -Encoding UTF8

Write-Host "Build+deploy succeeded."
Write-Host "DLL: $dllPath"
if ($SkipDeploy) {
  Write-Host "Deployed: skipped"
} else {
  Write-Host "Deployed: $destinationDll"
}
Write-Host "Report: $reportPath"
