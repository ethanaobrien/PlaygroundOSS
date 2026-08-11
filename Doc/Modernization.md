# PlaygroundOSS modernization

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
| Windows | Legacy Visual Studio/Win32 runtime | Native MSVC preset and SDL3 host | Connect the engine adapter |
| Linux | Modern SDL3/GLES desktop bootstrap | Complete engine graph builds and official assets reach `m_boot/start.lua` | Replace temporary mute/unsupported services and validate longer flows |
| macOS | Legacy Xcode/Cocoa runtime | Native Clang preset and SDL3 host | Connect the engine adapter |

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

Current Linux limitations are explicit: audio is a mute lifecycle-compatible
backend; native text widgets, movies, store, ads, notifications, location, and
motion are unavailable; secure/user-default storage is process-local; and
encrypted write streams are not implemented yet. These services return
conservative unavailable results instead of emulating successful platform
behavior. They are the next portability work, not engine-source patches.
The measured implementation scope and priority order are maintained in
[LinuxPlatformGapAnalysis.md](LinuxPlatformGapAnalysis.md).

`playground-engine-link-probe` links every engine object with whole-archive
semantics. The SDL engine executable does the same because Lua API libraries
register through static constructors and must not be discarded as apparently
unreferenced archive members. `playground-stream-probe` provides a focused
encrypted-read diagnostic.

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

The `modernization-platform` workflow compile-checks the SDL3 host on Linux,
Windows, and macOS, runs its headless lifecycle smoke check on Linux, and checks
the portable contract on all four Android ABIs. This is a build boundary gate,
not the deferred engine gameplay test suite.

## Compatibility policy

Build-system and platform-foundation changes must not modify files on the
`mostly-exact` reference. Focused comparison is required when a later change
crosses an observable engine boundary; a general gameplay trace harness is not
a prerequisite for this build-only checkpoint.
