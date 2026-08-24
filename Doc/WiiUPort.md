# Nintendo Wii U port

The Wii U target builds the complete reconstructed engine as a PowerPC RPX
and, when an external `AppAssets.zip` is supplied, packages a directly
launchable WUHB. The build securely expands the archive into read-only WUHB
content so the weaker console does not spend several minutes decompressing
8,462 small files on every clean test installation.

## Runtime design

- Graphics use the pinned Wii U/GX2 ANGLE fork and CafeGLSL runtime. The same
  GLES2 engine renderer is presented to both the TV and GamePad.
- VPAD supplies calibrated touch, buttons, and motion angles. The host keeps
  the active contact across `VPAD_READ_NO_SAMPLES`/busy polls and reports the
  last calibrated coordinate on release, so a tap is one uninterrupted
  down/up sequence. B maps to the Android-compatible Back key and Plus
  requests a clean exit.
- Text and password controls use the Wii U system software keyboard.
- Audio uses AX voices with linear 44.1 kHz to 48 kHz conversion, triple
  buffering, and stereo TV and GamePad routes.
- HTTP and HTTPS use devkitPro curl/mbedTLS. The WUHB carries the pinned
  2026-08-13 Mozilla CA bundle and every curl handle explicitly enables peer
  and hostname verification against `/vol/content/cacert.pem`; certificate
  verification is never disabled. The runtime also clears devkitPro curl's
  default `CAPATH`: it points at CafeOS's title-owned `scerts` directory,
  which homebrew cannot read, and mbedTLS otherwise rejects the connection
  even when the bundled `CAINFO` file is valid. The runtime also replaces the
  weak tick-seeded mbedTLS entropy hook with a persistent per-user HMAC-DRBG
  mixed with high-resolution timing and raw VPAD sensor samples.
- H.264, MPEG-4, MPEG-2, MJPEG, MOV, and MPEG movie playback use a pinned,
  static FFmpeg 6.1.2 cross-build.
- ProcUI acquire/release callbacks pause game and audio, cancel active touch,
  commit storage, and resume notification/audio/game state in that order.

The active Wii U user is obtained through `nn::act`. Bundled AppAssets are
immutable title content. Small durable state uses the account's title SaveData;
downloaded packages and on-demand assets use an account-scoped directory on
external storage so they do not consume the title SaveData quota:

| Engine root | Wii U location | Contents |
| --- | --- | --- |
| install | `/vol/content/AppAssets` | Read-only AppAssets packaged in the WUHB |
| external (nested paths) | `fs:/vol/external01/wiiu/apps/PlaygroundOSS/users/<persistent-id>/external` | Downloaded packages and on-demand assets |
| state / database-open requests | `/vol/save/<persistent-id>/state` | Identity, preferences, cookies, and writable databases |

Every SaveData durability boundary calls `SAVEUpdateSaveDir`. Asset downloads
use temporary files plus an atomic publish operation. A different Wii U account
receives a different persistent-ID directory in both storage classes; data is
never shared through a generic root-level SD folder. The bundled CafeGLSL RPL
is platform runtime code rather than user data; the host installs its pinned
copy in `sd:/wiiu/libs` when needed by Aroma.

The build rejects unsafe paths, links, duplicates, case collisions, and a
missing `start.lua` before expanding AppAssets. WUHB content is integrity-
protected and read-only at runtime. The console therefore performs only a
small required-file check before starting the engine; there is no first-launch
archive extraction or partially published install generation.

## Build with devkitPro

Install the Wii U devkitPro toolchain and portlibs, including WUT, curl,
mbedTLS, FreeType, PNG, zlib, bzip2, and Brotli. CMake downloads pinned ANGLE,
CafeGLSL, and FFmpeg sources on the first build.

Extract only the archive entry from the community APK, without extracting the
archive itself:

```sh
unzip -p /path/lovelive-community.apk assets/AppAssets.zip \
  > /tmp/AppAssets.zip
```

Configure and build:

```sh
/opt/devkitpro/portlibs/wiiu/bin/powerpc-eabi-cmake \
  -S . -B out/build/wiiu-release \
  -DCMAKE_BUILD_TYPE=Release \
  -DPLAYGROUND_TARGET_PLATFORM=wiiu \
  -DPLAYGROUND_BUILD_ENGINE_RUNTIME=ON \
  -DPLAYGROUND_BUILD_WIIU_HOST=ON \
  -DPLAYGROUND_APP_ASSETS_ZIP=/tmp/AppAssets.zip

cmake --build out/build/wiiu-release -j"$(nproc)" \
  --target playground-wiiu-engine_wuhb
```

The important outputs are:

```text
out/build/wiiu-release/Engine/modern/host/wiiu/playground-wiiu-engine.elf
out/build/wiiu-release/Engine/modern/host/wiiu/playground-wiiu-engine.rpx
out/build/wiiu-release/Engine/modern/host/wiiu/playground-wiiu-engine.wuhb
```

The WUHB contains `root.rpx`, the expanded read-only `AppAssets/` tree,
generated integrity metadata, the pinned Mozilla `cacert.pem`, and the pinned
`glslcompiler.rpl`. Building without
`PLAYGROUND_APP_ASSETS_ZIP` still produces the host probe and full engine RPX,
but deliberately disables the distributable engine WUHB.

## Reproducible Docker build

When devkitPro is not installed locally:

```sh
docker run --rm \
  -v "$PWD:/work" -v /tmp:/host-tmp -w /work \
  devkitpro/devkitppc:latest \
  /opt/devkitpro/portlibs/wiiu/bin/powerpc-eabi-cmake \
  -S . -B out/build/wiiu-release \
  -DCMAKE_BUILD_TYPE=Release \
  -DPLAYGROUND_TARGET_PLATFORM=wiiu \
  -DPLAYGROUND_BUILD_ENGINE_RUNTIME=ON \
  -DPLAYGROUND_BUILD_WIIU_HOST=ON \
  -DPLAYGROUND_APP_ASSETS_ZIP=/host-tmp/AppAssets.zip

docker run --rm \
  -v "$PWD:/work" -v /tmp:/host-tmp -w /work \
  devkitpro/devkitppc:latest \
  cmake --build out/build/wiiu-release -j8 \
  --target playground-wiiu-engine_wuhb
```

## Run in Cemu

Cemu 2.6 accepts the WUHB directly:

```powershell
C:\path\to\Cemu.exe -g C:\path\to\playground-wiiu-engine.wuhb
```

AppAssets are read directly from WUHB content; identity and writable databases
remain in emulated title SaveData, while downloaded assets use the emulated SD
root for the active account. Cemu must have a usable emulated account and a player-one
controller profile whose emulated controller is **Wii U GamePad**. Mouse input
only becomes VPAD touch when it is used over Cemu's separate GamePad View; a
click on the TV view is intentionally not a touchscreen event. Enable
`Options > Separate GamePad view` (or press Ctrl+Tab to switch the main view)
before testing touch. Its log is normally under `%APPDATA%\Cemu\log.txt`;
SaveData is under the configured MLC root and external assets under Cemu's
emulated SD root.

For RPX-only diagnostics, create the normal Cemu layout:

```text
PlaygroundOSS-Cemu/
  code/root.rpx
  content/AppAssets/
  content/AppAssets.metadata
  content/cacert.pem
  content/glslcompiler.rpl
```

Launching the WUHB is preferred because it assigns a stable homebrew title ID
and therefore exercises the intended per-title SaveData path.

Cemu may print a pre-launch `Save path ... (not present)` line for title
category `0005000f`. That is not the WUHB application's SaveData mount. WUT's
homebrew Save API stores this title under category `00050003`; after startup,
the account directory appears below
`mlc01/usr/save/00050003/6a5ffaff/user/<persistent-id>`. A successful bootstrap
also creates the matching account directory below
`sdcard/wiiu/apps/PlaygroundOSS/users/<persistent-id>`. The application log
reaching `startup callback complete` and `m_login/start.lua`, together with an
updated `state/entropy.seed`, proves that account discovery, SaveData commit,
asset validation, and engine startup completed.

## Run on hardware

Use an Aroma-compatible environment and copy the WUHB to a title-scoped
directory such as:

```text
sd:/wiiu/apps/PlaygroundOSS/playground-wiiu-engine.wuhb
```

Launch it as the account whose game state should be used. AppAssets are already
inside the WUHB and require no separate SD-card extraction or installation.
Hardware release acceptance is tracked in
[`WiiUHardwareAcceptance.md`](WiiUHardwareAcceptance.md).
