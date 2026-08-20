# Building and running the modernization branch

This guide applies to the `modernization` branch. Run every command from the
repository root unless noted otherwise.

## Supported targets

| Target | Modern build | What can be run |
| --- | --- | --- |
| Linux x86-64 | Complete reconstructed engine and SDL3/GLES2 host | Full game, host diagnostic, and engine probes |
| Windows x86-64 | Complete reconstructed engine and SDL3/ANGLE host | Full game, host diagnostic, and engine probes |
| WebAssembly (Emscripten wasm32) | Complete threaded engine and SDL3/WebGL host | Full game in a cross-origin-isolated browser |
| macOS | Portable contract and SDL3 host | Host and platform diagnostics; the full engine adapter is not connected yet |
| Android ARM64, ARMv7, x86, x86-64 | Portable contract static library | Cross-build validation only; these presets do not create or install an APK |

CMake 3.24 or newer and Git are required everywhere. The first desktop
configuration may download the SDL 3.4.12 source archive pinned in
`cmake/PlaygroundSDL.cmake`.

## Application assets

The full Linux development build and explicit-root Windows development run
require an extracted `AppAssets.zip`. Official
application assets are immutable inputs and are not included in this repository.
Extract them outside the source and build trees. The path supplied as
`--install-root` must directly contain `start.lua`, `assets`, `db`, `m_boot`,
and the other application directories.

Always give the game a separate writable `--external-root`. Initial packages,
on-demand downloads, databases, secure/default state, screenshots, and other
runtime data are written there. The install root is only read.

Example extraction commands are:

```sh
mkdir -p "$HOME/PlaygroundOSS-AppAssets"
unzip /path/to/AppAssets.zip -d "$HOME/PlaygroundOSS-AppAssets"
```

```powershell
Expand-Archive C:\path\to\AppAssets.zip C:\PlaygroundOSS-AppAssets
```

After extraction, verify that `start.lua` is immediately inside the selected
directory rather than one additional nested `AppAssets` directory down.

The game executable accepts:

```text
playground-sdl-engine [--install-root path] [--external-root path] [--frames n]
```

Omit `--frames` for an interactive session. Close the window or press F4 to
quit. Escape is delivered to the game as Android's Back key.

## Linux

### Prerequisites

Fedora:

```sh
sudo dnf install cmake ninja-build gcc gcc-c++ pkgconf-pkg-config \
    libglvnd-devel libcurl-devel freetype-devel openssl-devel libpng-devel \
    zlib-ng-compat-devel ffmpeg-free-devel
```

Ubuntu and Debian:

```sh
sudo apt-get update
sudo apt-get install cmake ninja-build build-essential pkg-config \
    libgles2-mesa-dev libcurl4-openssl-dev libfreetype-dev libssl-dev \
    libpng-dev zlib1g-dev libavcodec-dev libavformat-dev libavutil-dev \
    libswscale-dev
```

### Configure and build

Release:

```sh
cmake --preset linux-release
cmake --build --preset linux-release -j"$(nproc)"
```

For a Debug build, replace `linux-release` with `linux-debug` in both commands.
The relevant Release artifacts are:

```text
out/build/linux-release/Engine/modern/platform/playground-platform-info
out/build/linux-release/Engine/modern/host/sdl/playground-desktop-host
out/build/linux-release/Engine/modern/host/sdl/playground-sdl-engine
out/build/linux-release/Engine/modern/runtime/playground-*-probe
```

### Run

Verify the selected platform and SDL lifecycle:

```sh
./out/build/linux-release/Engine/modern/platform/playground-platform-info
./out/build/linux-release/Engine/modern/host/sdl/playground-desktop-host \
    --headless --frames 3
```

Run the game interactively:

```sh
mkdir -p "$HOME/.local/share/PlaygroundOSS-SIF"
./out/build/linux-release/Engine/modern/host/sdl/playground-sdl-engine \
    --install-root /path/to/AppAssets \
    --external-root "$HOME/.local/share/PlaygroundOSS-SIF"
```

The host needs a working X11 or Wayland display. For a bounded automated boot
on a display-less machine, provide a virtual X server and a frame limit:

```sh
xvfb-run -a \
  ./out/build/linux-release/Engine/modern/host/sdl/playground-sdl-engine \
    --install-root /path/to/AppAssets \
    --external-root /tmp/playground-user \
    --frames 600
```

Useful focused checks include:

```sh
runtime=out/build/linux-release/Engine/modern/runtime
"$runtime/playground-engine-link-probe"
"$runtime/playground-stream-probe" /path/to/AppAssets asset://start.lua
"$runtime/playground-platform-services-probe" /tmp/playground-services
"$runtime/playground-audio-probe" \
    /path/to/AppAssets /tmp/playground-audio \
    asset://assets/sound/voice/sticker/vo_st_109_0001
"$runtime/playground-movie-probe" /path/to/movie.mp4
```

## Windows

### Prerequisites

Install:

- Visual Studio 2022 or Build Tools 2022 with **Desktop development with C++**;
- CMake 3.24 or newer;
- Git;
- a vcpkg checkout at the baseline pinned by `vcpkg.json`.

Bootstrap the pinned dependency manager from PowerShell:

```powershell
git clone https://github.com/microsoft/vcpkg.git C:\src\vcpkg
git -C C:\src\vcpkg checkout dc31f86a441067d6c2e9e4ad52988eb41ef4f442
C:\src\vcpkg\bootstrap-vcpkg.bat -disableMetrics
$env:VCPKG_ROOT = "C:\src\vcpkg"
```

Keep `VCPKG_ROOT` set whenever the `windows-msvc` preset is configured. CMake
installs the manifest dependencies automatically.

### Configure and build

From a native Windows PowerShell prompt:

```powershell
cmake --preset windows-msvc
cmake --build --preset windows-msvc
```

Release artifacts are under the configuration subdirectory:

```text
out\build\windows-msvc\Engine\modern\platform\Release\playground-platform-info.exe
out\build\windows-msvc\Engine\modern\host\sdl\Release\playground-desktop-host.exe
out\build\windows-msvc\Engine\modern\host\sdl\Release\playground-sdl-engine.exe
out\build\windows-msvc\Engine\modern\runtime\Release\playground-*-probe.exe
```

The build copies SDL3, ANGLE EGL/GLES, FFmpeg, curl, OpenSSL, FreeType, PNG,
zlib, and their required DLLs beside the executables. No vcpkg `bin` directory
should be added to `PATH` to run the result.

### Run

Run the automated dependency and runtime checks:

```powershell
./scripts/test_windows_runtime.ps1 -SkipBuild
```

Include immutable official assets and optional audio/movie coverage with:

```powershell
./scripts/test_windows_runtime.ps1 -SkipBuild `
    -InstallRoot C:\path\to\AppAssets `
    -AudioAsset asset://assets/sound/voice/sticker/vo_st_109_0001 `
    -MoviePath C:\path\to\movie.mp4
```

Run the game interactively:

```powershell
$external = Join-Path $env:LOCALAPPDATA "PlaygroundOSS-SIF"
New-Item -ItemType Directory -Force $external | Out-Null
./out/build/windows-msvc/Engine/modern/host/sdl/Release/playground-sdl-engine.exe `
    --install-root C:\path\to\AppAssets `
    --external-root $external
```

For distribution, build the Inno Setup package described in
[`WindowsInstaller.md`](WindowsInstaller.md). An installed copy launches with
no arguments: it verifies and atomically extracts its bundled `AppAssets.zip`
to `%LOCALAPPDATA%\PlaygroundOSS-SIF\install`, and uses the persistent sibling
`external` directory for downloaded/runtime data.

WSL interoperability can launch the native Windows binaries and may pass a UNC
AppAssets path such as `\\wsl.localhost\FedoraLinux-43\tmp\sif-appassets`.
For compilation, prefer a Windows-local checkout: MSBuild lowercases some UNC
dependency records, which causes unnecessary rebuilds when the source resides
on WSL's case-sensitive filesystem.

## macOS

### Prerequisites

Install Xcode command-line tools, CMake, and Ninja. Homebrew is one convenient
source for the latter two:

```sh
xcode-select --install
brew install cmake ninja
```

### Configure, build, and run

```sh
cmake --preset macos-clang
cmake --build --preset macos-clang -j"$(sysctl -n hw.logicalcpu)"
./out/build/macos-clang/Engine/modern/platform/playground-platform-info
./out/build/macos-clang/Engine/modern/host/sdl/playground-desktop-host \
    --frames 300
```

Use `--headless --frames 3` for a noninteractive lifecycle check. The current
macOS preset intentionally builds the platform contract and SDL3 host only;
there is no `playground-sdl-engine` artifact on macOS yet. Do not point the host
diagnostic at AppAssets—it does not load the game.

## Android

### Prerequisites

Install CMake, Ninja, and Android NDK `27.2.12479018`, then set
`ANDROID_NDK_HOME` to that NDK directory. With Android command-line tools:

```sh
sdkmanager "ndk;27.2.12479018"
export ANDROID_NDK_HOME="$ANDROID_SDK_ROOT/ndk/27.2.12479018"
```

### Configure and build

Build any or all supported ABIs:

```sh
cmake --preset android-arm64-v8a
cmake --build --preset android-arm64-v8a

cmake --preset android-armeabi-v7a
cmake --build --preset android-armeabi-v7a

cmake --preset android-x86
cmake --build --preset android-x86

cmake --preset android-x86_64
cmake --build --preset android-x86_64
```

Each preset produces:

```text
out/build/<preset>/Engine/modern/platform/libplayground_platform_contract.a
```

### Run status

The modern Android presets are cross-compilation gates for the portable target
contract. They do not currently build an Activity, APK, JNI game library, or
installable package, so there is no honest `adb install` or run command for
these CMake outputs. The reconstructed Android engine remains available through
the existing Android integration outside this modern host target. Connecting
that proven runtime to these CMake presets is separate future work.

## WebAssembly / Emscripten

The web target is named `emscripten` throughout the CMake platform contract.
It is a wasm32, pthread-enabled build of the complete engine, hosted by SDL3
and WebGL. There is no single-threaded or non-isolated fallback.

### Prerequisites

Install Emscripten SDK 4.0.23, CMake, Ninja, Python 3, and Node.js. Activate the
SDK in the shell used for CMake so `EMSDK`, `emcc`, and `em++` are available:

```sh
git clone https://github.com/emscripten-core/emsdk.git "$HOME/emsdk"
cd "$HOME/emsdk"
git checkout 4.0.23
./emsdk install 4.0.23
./emsdk activate 4.0.23
source ./emsdk_env.sh
cd /path/to/PlaygroundOSS
```

### Configure and build

Pass the original archive itself; do not extract it into the web deployment.
The build copies it unchanged and generates integrity metadata used by the
transactional first-run installer:

```sh
cmake --preset web-release \
  -DPLAYGROUND_APP_ASSETS_ZIP=/path/to/AppAssets.zip
cmake --build --preset web-release -j"$(nproc)"
```

The deployable directory is:

```text
out/build/web-release/Engine/modern/host/web/
  index.html
  index.js
  index.wasm
  AppAssets.zip
  AppAssets.metadata
```

For a CDN-hosted archive, omit `PLAYGROUND_APP_ASSETS_ZIP`, provide generated
metadata with `PLAYGROUND_WEB_APP_ASSETS_METADATA`, and set
`PLAYGROUND_WEB_APP_ASSETS_URL` and
`PLAYGROUND_WEB_APP_ASSETS_METADATA_URL`. URLs are relative to the page unless
an absolute URL is supplied.

### Serve and run

Threaded WebAssembly requires cross-origin isolation. Every production server
must return at least these headers for the document and all subresources:

```text
Cross-Origin-Opener-Policy: same-origin
Cross-Origin-Embedder-Policy: require-corp
Cross-Origin-Resource-Policy: same-origin
```

The repository server supplies those headers for local development:

```sh
cmake --build out/build/web-release --target playground-web-serve
# Open http://127.0.0.1:8765/
```

Or start it directly:

```sh
python3 scripts/serve_web.py \
  out/build/web-release/Engine/modern/host/web --port 8765
```

The first launch downloads the unchanged archive, checks its size and SHA-256,
validates every ZIP path and CRC, and extracts it into a content-addressed OPFS
generation. The completion marker is the final write because OPFS cannot
atomically rename a populated directory. Interrupted generations are rejected
and rebuilt. Later launches download only the small metadata file and reuse the
installed generation.

Both installed and runtime data live in the browser's origin-private file
system with no picker or permission prompt:

```text
/playground-opfs/cache/appassets/install-<sha256>  immutable AppAssets
/playground-opfs/user                             state and downloaded assets
```

This uses WasmFS' OPFS backend directly and does not use IndexedDB. Storage is
scoped to the exact origin, so changing scheme, host, or port selects a
different game installation. Clearing site data deletes both installed and
user data. Startup requests the browser's durable-storage classification
without displaying a permission prompt. If the browser declines it, OPFS data
still survives ordinary closes and reloads but remains eligible for automatic
quota eviction under storage pressure.

The browser owns cookies and codec support. Remote game/API endpoints must
permit browser CORS requests; under COEP their responses must also be
CORS-enabled or served through a same-origin reverse proxy. Serve the page over
HTTPS in production (localhost is treated as a secure context). When the page
uses HTTPS, the Emscripten transport upgrades absolute `http://` request URLs
to `https://` before Fetch so legacy configuration cannot trigger active
mixed-content rejection. It never downgrades HTTPS URLs when the page is served
over HTTP; every upgraded endpoint must therefore support TLS. Geolocation,
device orientation, notifications, clipboard, SDL audio, HTML video decoding,
and WebGL are connected to browser APIs. Remote push, purchases, and rewarded
ads need application providers and therefore report unavailable/failure rather
than fabricating success.

Run the storage/thread/SQLite qualification and full-engine boot checks with
Chromium as follows:

```sh
./scripts/build_web_platform_probe.sh
python3 scripts/serve_web.py out/web-platform-probe --port 8765 &
node scripts/test_web_platform_probe.mjs http://127.0.0.1:8765/

python3 scripts/serve_web.py \
  out/build/web-release/Engine/modern/host/web --port 8766 &
node scripts/test_web_engine.mjs http://127.0.0.1:8766/ \
  /tmp/playground-web-engine-profile
```

Set `PLAYGROUND_CHROMIUM` if the executable is not named `chromium-browser`.
The full-engine test uses a persistent profile, verifies isolation, and waits
for the first engine frame. Set `PLAYGROUND_WEB_TEST_CLICK=1` to dispatch a
title-screen click and observe the login transition after boot.

## Configuration controls

- `PLAYGROUND_SDL_PROVIDER=auto|system|fetch` selects an installed SDL3 package
  or the pinned source fallback. `auto` is the default.
- `PLAYGROUND_BUILD_ENGINE_RUNTIME` is enabled by default on Linux and Windows
  and disabled on macOS and Android.
- `PLAYGROUND_BUILD_DESKTOP_HOST` is enabled for native desktop builds.
- `PLAYGROUND_BUILD_PLATFORM_PROBE` is disabled while cross-compiling.
- `PLAYGROUND_APP_ASSETS_ZIP` packages the original archive for the targets
  with transactional first-run extraction, including Emscripten.
- `PLAYGROUND_LANGUAGE`, `PLAYGROUND_COUNTRY`, `PLAYGROUND_LOCATION` (formatted
  as `latitude,longitude`), and `PLAYGROUND_MOTION` (formatted as
  `azimuth,elevation`) override deterministic desktop defaults at runtime.

The CI definition in `.github/workflows/modernization-platform.yml` is the
canonical unattended build matrix for Linux, Windows, Emscripten, macOS, and
all four Android ABIs.
