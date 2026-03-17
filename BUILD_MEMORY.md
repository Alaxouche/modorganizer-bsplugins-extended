# Build Memory

This file is a short build playbook for this repository, intended for humans and AI agents.

## Goal

Build `bsplugins.dll` for the local MO2 runtime at `F:/Games/Wunduniik Modlist`.

## Verified Environment

- Repo: `D:/Code/projects/modorganizer-bsplugins-main`
- CMake: `D:/Code/tools/cmake/bin/cmake.exe`
- Shared install prefix: `D:/Code/projects/install`
- Runtime target: `F:/Games/Wunduniik Modlist`
- Runtime `uibase.dll`: loaded from the modlist root, not `dlls/`
- Runtime-compatible `uibase`: MO2 2.5.2 ABI

## Important Constraints

- Build `bsplugins` in `Release`.
- Do not rely on a default `Debug` build in CMake Tools here.
- Local `bsatk.lib` is built as `Release`; a `Debug` link causes `LNK2038` runtime library and iterator debug mismatches.
- `bsplugins.dll` must not import `fmt.dll` or `zlib1.dll`.

## Build Command

From the repository root:

```powershell
& 'D:\Code\tools\cmake\bin\cmake.exe' --build vsbuild --config Release
```

If `vsbuild` does not exist yet:

```powershell
& 'D:\Code\tools\cmake\bin\cmake.exe' --preset windows-2022
& 'D:\Code\tools\cmake\bin\cmake.exe' --build vsbuild --config Release
```

## Output

- DLL: `vsbuild/src/Release/bsplugins.dll`
- Import lib: `vsbuild/src/Release/bsplugins.lib`

## Deploy Command

```powershell
Copy-Item 'D:\Code\projects\modorganizer-bsplugins-main\vsbuild\src\Release\bsplugins.dll' 'F:\Games\Wunduniik Modlist\plugins\bsplugins.dll' -Force
```

## One-shot Build + Deploy

```powershell
./scripts/build-deploy.ps1
```

This script:

- builds `bsplugins` in `Release`;
- deploys `bsplugins.dll` to the MO2 `plugins` folder;
- fails if forbidden dependencies (`fmt.dll`, `zlib1.dll`) are detected;
- writes `vsbuild/src/Release/build-report.txt` with timestamp, hash, and dependency list.

## Fast Runtime Probe

Use this exact probe to test whether the deployed plugin loads in the same search path MO2 uses:

```powershell
$plugin='F:\Games\Wunduniik Modlist\plugins\bsplugins.dll';
$base='F:\Games\Wunduniik Modlist';
$oldPath=$env:PATH;
$env:PATH="$base;$base\dlls;$base\plugins;" + $oldPath;
try {
  $h=[System.Runtime.InteropServices.NativeLibrary]::Load($plugin);
  Write-Host 'LOAD_OK';
  [System.Runtime.InteropServices.NativeLibrary]::Free($h)
} catch {
  Write-Host ('LOAD_FAIL: ' + $_.Exception.Message)
}
$env:PATH=$oldPath
```

## Source Compatibility Notes

This repo currently contains compatibility changes for the local MO2 2.5.2 stack:

- `TESData::PluginList` removes `override` from methods that do not exist in uibase 2.5.2's `IPluginList`.
- `SafeWriteFile` calls use `file.commit()` instead of `file->commit()`.
- Empty plugin groups are persisted and restored.
- `lockedorder.txt` snapshots are preserved and restored.
- Plugin grouping can be disabled with the plugin setting `enable_plugin_grouping`.

## CMake Tools Warning

If using the CMake Tools VS Code build button, verify that it is not silently building `Debug`. In this workspace, the safe path is the explicit terminal build above with `--config Release`.