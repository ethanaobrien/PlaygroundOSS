# PlaygroundOSS modernization

The `mostly-exact` branch at commit `c67d46e` is the protected compatibility
reference. Modern infrastructure work is developed on `modernization`; the
legacy engine source and platform projects remain unchanged until their
replacement boundary is ready.

## Current checkpoint

The root CMake project establishes one target-platform vocabulary for Android,
Windows, Linux, and macOS. It currently builds a small C platform contract and
host diagnostic. It does **not** claim that the complete legacy engine already
builds on every host.

| Platform | Existing runtime | Modern CMake status | Next host work |
| --- | --- | --- | --- |
| Android | Reconstructed JNI/GLES/OpenSL runtime | Configures for all four ABIs | Move the proven source graph behind CMake |
| Windows | Legacy Visual Studio/Win32 runtime | Native MSVC preset | Introduce SDL3 host alongside Win32 reference |
| Linux | No legacy runtime | Native GCC/Clang preset and probe | Implement SDL3 window/input/lifecycle host |
| macOS | Legacy Xcode/Cocoa runtime | Native Clang preset | Enable after the SDL3 desktop seam stabilizes |

## Configure and build

Linux:

```sh
cmake --preset linux-debug
cmake --build --preset linux-debug
./out/build/linux-debug/Engine/modern/platform/playground-platform-info
```

Android uses the NDK CMake toolchain and deliberately builds only the portable
contract in this checkpoint:

```sh
export ANDROID_NDK_HOME=/path/to/android-ndk
cmake --preset android-arm64-v8a
cmake --build --preset android-arm64-v8a
```

Equivalent presets exist for `armeabi-v7a`, `x86`, and `x86_64`.

The `modernization-platform` workflow compile-checks the portable contract on
Linux, Windows, macOS, and all four Android ABIs. This is a build boundary gate,
not the deferred engine gameplay test suite.

## Compatibility policy

Build-system and platform-foundation changes must not modify files on the
`mostly-exact` reference. Focused comparison is required when a later change
crosses an observable engine boundary; a general gameplay trace harness is not
a prerequisite for this build-only checkpoint.
