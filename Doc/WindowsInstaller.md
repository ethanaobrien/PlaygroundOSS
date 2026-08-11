# Windows installer and asset bootstrap

The Windows distribution is self-contained. It installs the executable,
app-local DLL dependency closure, the Microsoft Visual C++ redistributable,
and the immutable `AppAssets.zip` bundle under Program Files. Official assets
remain an external build input and are never committed to this repository.

## Runtime layout

This mirrors the Android wrapper's app-scoped `install` and `external`
siblings. A normal no-argument launch uses:

```text
%ProgramFiles%\PlaygroundOSS SIF\
  playground-sdl-engine.exe
  *.dll
  AppAssets.zip
  AppAssets.version

%LOCALAPPDATA%\PlaygroundOSS-SIF\
  install\                 extracted immutable AppAssets
  external\                downloads, databases, settings, and save data
  logs\bootstrap.log       first-run/update diagnostic log
  install.new\             temporary extraction, only while updating
  install.old\             rollback copy, only while updating
```

The executable verifies the archive size and SHA-256 recorded in
`AppAssets.version` before extraction. Every zip entry is CRC-checked; absolute
paths, parent traversal, invalid UTF-8 names, and symbolic links are rejected.
Extraction occurs in `install.new`. A completed tree is renamed into place,
with the previous install kept as `install.old` until activation succeeds.
This makes a process termination or machine restart during extraction
recoverable on the next launch. Upgrades replace only `install`; `external` is
never deleted or reset.

The installer uninstalls Program Files content by default while preserving
the Local AppData tree. Interactive uninstall asks whether user data should
also be deleted. Automation can request deletion with:

```powershell
unins000.exe /VERYSILENT /REMOVEUSERDATA=1
```

## Build an installer

Install Inno Setup 6 and build the Release engine as described in
`Doc/BuildAndRun.md`, then run:

```powershell
./scripts/build_windows_installer.ps1 `
  -BuildDirectory out/build/windows-msvc `
  -Configuration Release `
  -AppAssetsZip C:\path\to\AppAssets.zip `
  -Version 9.11.0
```

The script:

1. stages only `playground-sdl-engine.exe`, its app-local DLLs, the asset
   archive/manifest, and `vc_redist.x64.exe`;
2. calculates the archive's SHA-256 and byte length;
3. compiles `installer/windows/PlaygroundOSS-SIF.iss`; and
4. prints the installer path and SHA-256.

The default output is
`out/package/windows/PlaygroundOSS-SIF-Setup-<version>-x64.exe`. Override Inno
Setup or redistributable discovery with `-ISCCPath` and `-VCRedistPath` when
using a nonstandard Visual Studio installation.

The `Windows installer` GitHub workflow performs the same build. Automatic
branch runs use a deterministic generated AppAssets fixture, keeping the full
installer lifecycle gate independent of an asset host or anti-bot service.
Manual release dispatches instead download the configured immutable APK input,
extract only `assets/AppAssets.zip`, and record both input hashes. Both modes
run the native/runtime/bootstrap gates, build the installer, and exercise
silent fresh install, default-root launch, same-version upgrade, persistent
user data, default uninstall, and explicit data-removal uninstall before
uploading the workflow artifact.

## Verification

The synthetic test does not need official assets:

```powershell
./scripts/test_windows_bootstrap.ps1 -SkipBuild
```

It exercises a fresh install, current-version relaunch, Unicode data root,
upgrade, forced process termination during extraction, rollback recovery,
persistent external data, corrupt fresh-install and corrupt-upgrade rejection,
and archive traversal rejection. The broader Windows runtime gate invokes it
automatically:

```powershell
./scripts/test_windows_runtime.ps1 -SkipBuild
```

For an installed official bundle, launch from the Start Menu normally. For a
bounded diagnostic launch from PowerShell, use:

```powershell
& "$env:ProgramFiles\PlaygroundOSS SIF\playground-sdl-engine.exe" --frames 120
```

Do not provide roots in the installed case; no-argument/default launch is the
bootstrap path being validated. Explicit `--install-root` and
`--external-root` remain available for source-tree development and bypass the
bundled-asset bootstrap.
