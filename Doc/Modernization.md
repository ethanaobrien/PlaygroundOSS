# PlaygroundOSS modernization

The `mostly-exact` branch at commit `c67d46e` is the protected compatibility
reference. Modern infrastructure work is developed on `modernization`; the
legacy engine source and platform projects remain unchanged until their
replacement boundary is ready.

## Current checkpoint

The root CMake project establishes one target-platform vocabulary for Android,
Windows, Linux, and macOS. It builds a portable platform contract plus an SDL3
desktop lifecycle, window, OpenGL, input, and high-DPI seam. It does **not**
claim that the complete legacy engine is connected to that seam yet.

| Platform | Existing runtime | Modern CMake status | Next host work |
| --- | --- | --- | --- |
| Android | Reconstructed JNI/GLES/OpenSL runtime | Configures for all four ABIs | Move the proven source graph behind CMake |
| Windows | Legacy Visual Studio/Win32 runtime | Native MSVC preset and SDL3 host | Connect the engine adapter |
| Linux | No legacy runtime | Native GCC/Clang preset and verified SDL3 host | Connect the engine adapter |
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
