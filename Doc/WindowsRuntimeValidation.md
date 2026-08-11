# Windows runtime validation

## Completion status

The modernization branch builds the complete reconstructed engine graph with
Visual Studio 2022 for native x86-64 Windows. The runtime uses SDL3 for the host
and audio boundary, ANGLE EGL/OpenGL ES 2 for rendering, the same FreeType,
curl, OpenSSL, PNG, zlib, and FFmpeg engine services as Linux, and the existing
desktop persistence and mobile-service adapters.

The dependency graph is described by the root `vcpkg.json` and its pinned
builtin baseline. SDL3 is fetched at the hash recorded in
`cmake/PlaygroundSDL.cmake`. CMake's vcpkg app-local deployment plus explicit
SDL3/ANGLE deployment places the full DLL closure beside each executable. The
game does not depend on an SDK DLL directory being present in `PATH`.

## Verified runtime surface

The following checks were executed through WSL interoperability against native
Windows executables built by MSVC 19.44:

- all 550 engine and vendored translation units compile and the whole-archive
  engine link probe resolves every static-registration object;
- the headless SDL lifecycle and Windows platform-services probe pass;
- persistent state, encrypted writes, request/device-integrity data, worker
  threads, memory queries, and virtual-path temporary-file publication pass;
- immutable official `asset://start.lua` decrypts to the expected 5,476-byte
  Lua source through a UNC install root;
- a shipped Ogg voice asset loads, decodes, plays, and stops through SDL audio;
- a synthetic MPEG-4 file opens and produces a 64x48 FFmpeg movie frame;
- the full game creates an EGL/GLES2 context through ANGLE without environment
  overrides, enters `start.lua` and `m_boot/start.lua`, and exits cleanly after
  a bounded run;
- a disposable mirror of the known-good Linux external state reaches
  `m_login/start.lua` through a real Windows pointer event and continues
  rendering after the title transition. The Linux state and immutable install
  assets are never modified by this check.

The initial renderer failure was caused by mixing SDL's desktop WGL context
with symbols provided by ANGLE's GLES library. The host now sets
`SDL_HINT_VIDEO_FORCE_EGL` whenever the engine requests the GLES profile and
CMake deploys both `libEGL.dll` and `libGLESv2.dll`. The automated PowerShell
gate asserts that exact runtime closure before starting any probe.

## Reproduction

From a native Windows checkout with Visual Studio 2022 and a bootstrapped vcpkg
checkout:

```powershell
$env:VCPKG_ROOT = "C:\src\vcpkg"
cmake --preset windows-msvc
cmake --build --preset windows-msvc
./scripts/test_windows_runtime.ps1 -SkipBuild
```

Add `-InstallRoot`, `-AudioAsset`, and `-MoviePath` as documented in
[Modernization.md](Modernization.md) to reproduce the official-data checks.
The CI workflow performs the asset-independent build/link/host/platform subset
because official application assets are intentionally not stored in this
repository.

## Platform distinctions

The same honest desktop distinctions documented for Linux apply on Windows:
mobile purchases and rewarded ads report normal failure callbacks, web controls
use the system browser, and sensor inputs use explicit deterministic overrides.
Windows notifications use an SDL message box. These are complete desktop
semantics, not silent success stubs.
