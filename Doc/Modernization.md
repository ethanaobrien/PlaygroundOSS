# PlaygroundOSS modernization

For prerequisites and exact build/run commands on Linux, Windows, macOS, and
Android, start with [BuildAndRun.md](BuildAndRun.md). Nintendo Switch uses a
separate installed-title/storage model documented in
[SwitchPort.md](SwitchPort.md).

The `mostly-exact` branch at commit `c67d46e` is the protected compatibility
reference. Modern infrastructure work is developed on `modernization`; the
legacy engine source and platform projects remain unchanged until their
replacement boundary is ready.

## Current checkpoint

The root CMake project establishes one target-platform vocabulary for Android,
Windows, Linux, and macOS. It builds a portable platform contract plus an SDL3
desktop lifecycle, window, OpenGL ES, input, and high-DPI seam. On Linux it now
also compiles and links the complete reconstructed engine graph and drives the
original `GameSetup`, `initGame`, `frameFlip`, and `finishGame` lifecycle.

| Platform | Existing runtime | Modern CMake status | Next host work |
| --- | --- | --- | --- |
| Android | Reconstructed JNI/GLES/OpenSL runtime | Configures for all four ABIs | Move the proven source graph behind CMake |
| Windows | Modern SDL3/ANGLE desktop runtime | Complete engine graph, desktop services, audio, widgets, and movies | Continue official-asset gameplay validation alongside Linux |
| Linux | Modern SDL3/GLES desktop runtime | Complete engine graph, desktop services, audio, widgets, movies, community login, package updates, and on-demand assets | Continue ordinary gameplay validation as new flows are exercised |
| macOS | Legacy Xcode/Cocoa runtime | Native Clang preset and SDL3 host | Connect the engine adapter |
| Nintendo Switch | Clean libnx modernization port | Complete engine NRO, transactional bundled assets, account SaveData, CacheStorage, audout, NIFM/curl, EGL/GLES2, lifecycle, album export, and audited NSP staging | Run and record the installed-title hardware acceptance matrix |

## Configure and build

Linux:

```sh
cmake --preset linux-debug
cmake --build --preset linux-debug
./out/build/linux-debug/Engine/modern/platform/playground-platform-info
./out/build/linux-debug/Engine/modern/host/sdl/playground-desktop-host --frames 3
```

The desktop diagnostic opens a real OpenGL window. For build agents or other
display-less environments, run the same lifecycle without a window:

```sh
./out/build/linux-debug/Engine/modern/host/sdl/playground-desktop-host \
    --headless --frames 3
```

The Linux engine host uses a GLES 2 context because that is the rendering API
expected by this engine generation. Extract the immutable application asset
archive outside the source tree, then run:

```sh
unzip /path/to/AppAssets.zip -d /tmp/sif-appassets
./out/build/linux-debug/Engine/modern/host/sdl/playground-sdl-engine \
    --install-root /tmp/sif-appassets \
    --external-root /tmp/sif-user \
    --frames 600
```

The install root is read-only by convention. `file://external` is mapped to the
separate writable external root, while `asset://` checks the external override
first and then the install root. Shipped encrypted reads use the reconstructed
`CDecryptBaseClass` with the same virtual-path key as Android.

The Linux runtime now provides persistent atomic secure/default storage and a
stable device ID, OpenSSL implementations of the engine RSA/AES contracts,
request-ID and device-integrity payloads, encrypted reads and writes, an SDL3
audio output backed by the reconstructed mixer and Ogg decoder, text/password
widgets, external-browser web and movie-widget fallbacks, FFmpeg movie textures,
clipboard/screenshot/sleep/locale/memory integration, notifications, and
configurable location/motion data. Purchases and rewarded ads cannot honestly
succeed on a desktop community build; those adapters issue the engine's normal
failure callbacks instead of silently succeeding or disappearing.

The immutable official application assets have also been exercised
interactively against NPPS4. The desktop runtime presents the protocol's
Android platform family, completes the Android-compatible ATT callback used by
the login chain, downloads the initial package set, and publishes on-demand
assets atomically from their trailing-underscore temporary files. Virtual
paths are resolved on both sides of that rename, so downloaded external assets
immediately override their install-root counterparts exactly as ordinary
`asset://` reads expect.

Set `PLAYGROUND_LANGUAGE`, `PLAYGROUND_COUNTRY`, `PLAYGROUND_LOCATION` (as
`latitude,longitude`), or `PLAYGROUND_MOTION` (as `azimuth,elevation`) to
override deterministic desktop defaults. The implementation inventory and its
validation evidence are maintained in
[LinuxPlatformGapAnalysis.md](LinuxPlatformGapAnalysis.md). Native MSVC build,
deployment, and official-data evidence is recorded separately in
[WindowsRuntimeValidation.md](WindowsRuntimeValidation.md).

`playground-engine-link-probe` links every engine object with whole-archive
semantics. The SDL engine executable does the same because Lua API libraries
register through static constructors and must not be discarded as apparently
unreferenced archive members. `playground-stream-probe` provides a focused
encrypted-read diagnostic. `playground-platform-services-probe` covers durable
identity/state, RSA/AES, request headers, encrypted-write round trips, memory,
threads, text input, and temporary-to-final on-demand asset publication.
`playground-audio-probe` decodes an engine audio asset
through the real SDL output path, and `playground-movie-probe` validates the
FFmpeg decoder independently of game Lua.

Linux engine builds require development packages for SDL3 (or the pinned fetch
fallback), GLES2, curl, FreeType, OpenSSL, PNG, zlib, and FFmpeg's avcodec,
avformat, avutil, and swscale libraries.

Windows builds use Visual Studio 2022 and a pinned vcpkg manifest. Bootstrap a
vcpkg checkout, set `VCPKG_ROOT`, and build the native preset from a Windows
PowerShell prompt:

```powershell
cmake --preset windows-msvc
cmake --build --preset windows-msvc
./scripts/test_windows_runtime.ps1 -SkipBuild
```

The Windows runtime uses SDL3 with ANGLE's EGL/OpenGL ES 2 implementation, not
desktop WGL. CMake deploys SDL3, `libEGL`, `libGLESv2`, and the vcpkg runtime
dependency closure beside the executables. The verification script fails if
that graphics closure is incomplete and then exercises the whole-archive
engine link, headless SDL lifecycle, and persistent desktop services. It can
also validate immutable official assets and optional audio/movie inputs:

```powershell
./scripts/test_windows_runtime.ps1 -SkipBuild `
    -InstallRoot C:\path\to\AppAssets `
    -AudioAsset asset://assets/sound/voice/sticker/vo_st_109_0001 `
    -MoviePath C:\path\to\probe.mp4
```

When invoking the native toolchain through WSL interop, prefer a Windows-local
source checkout and build directory. MSBuild lowercases dependency paths in a
few generated records; a case-sensitive `\\wsl.localhost` source tree can
therefore cause harmless but expensive rebuilds. Runtime paths may still be
UNC paths, so an immutable AppAssets directory under WSL can be tested without
copying or modifying it.

`PLAYGROUND_SDL_PROVIDER` accepts `auto`, `system`, or `fetch`. The default
first uses an installed SDL 3.4 package and otherwise fetches the pinned SDL
3.4.12 source release. This keeps Windows and macOS builds self-contained while
allowing Linux distributions to reuse their packaged SDL.

Android uses the NDK CMake toolchain and deliberately builds only the portable
contract in this checkpoint:

```sh
export ANDROID_NDK_HOME=/path/to/android-ndk
cmake --preset android-arm64-v8a
cmake --build --preset android-arm64-v8a
```

Equivalent presets exist for `armeabi-v7a`, `x86`, and `x86_64`.

The `modernization-platform` workflow builds the complete engine on Linux and
Windows, bootstraps the pinned Windows dependency graph, exercises the Windows
engine/link/platform probes, compile-checks the SDL3 host on macOS, and checks
the portable contract on all four Android ABIs. This is a reproducible platform
and runtime boundary gate, not a replacement for interactive official-asset
gameplay validation.

## Compatibility policy

Build-system and platform-foundation changes must not modify files on the
`mostly-exact` reference. Focused comparison is required when a later change
crosses an observable engine boundary; a general gameplay trace harness is not
a prerequisite for this build-only checkpoint.
