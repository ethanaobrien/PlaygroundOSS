# Nintendo Switch/libnx port

This document is the implementation contract for the clean libnx port. The
port starts from the `modernization` branch. Earlier experimental Switch
commits are evidence that the engine can run on the hardware, but their source
and commit history are not imported into this branch.

## Goal

Deliver a production-quality Nintendo Switch port as an installed, user-
selecting application title: bundle the original `AppAssets.zip` unchanged;
extract, validate, upgrade, and recover it transactionally in CacheStorage;
keep durable identity, credentials, preferences, and databases in the launch
user's SaveData; implement complete libnx graphics, input, audio, network,
lifecycle, notification, and system-service behavior; package with audited
NACP/NPDM least privilege; and pass both reproducible host-side gates and the
recorded real-hardware acceptance matrix. The development NRO may use only an
explicit title-scoped SD directory and is never accepted as production proof.

## Product and development modes

The production artifact is an installed application title. It must have a
stable, configurable application ID, a launch-selected user account, account
SaveData, application CacheStorage, and an immutable RomFS. Failure to acquire
any required production mount is a startup error. Production must never place
credentials, databases, extracted application files, or downloads in an
unscoped `sdmc:` directory.

The NRO is a development artifact. Because a homebrew NRO has no application
SaveData or CacheStorage of its own, it may use an explicitly selected,
title-namespaced SD directory. The program must identify that mode in its log
and UI. Development fallback is not shared with the installed title and must
not be selected automatically by the production executable.

## Storage contract

The engine sees three roots. Classification is explicit; it is never inferred
from whether a relative path happens to contain a slash.

| Root | Backing storage | Contents |
| --- | --- | --- |
| `install` | CacheStorage | Transactionally extracted `AppAssets.zip` |
| `content` | CacheStorage | Downloaded packages, temporary downloads, and other reproducible assets |
| `state` | Launch user's account SaveData | Credentials, device identity, preferences, cookies, and persistent databases |

The package contains the original `AppAssets.zip` as one RomFS file. It does
not contain an extracted AppAssets tree. On first launch or archive upgrade,
the bootstrap streams the archive into a staging generation under
CacheStorage, validates every entry and required engine files, commits the
generation, and atomically renames it to the immutable directory selected by
the bundled metadata hash. The bundled hash is the active-generation selector,
so there is no separately mutable active pointer to tear.

Bootstrap requirements:

- Reject absolute paths, `..` traversal, symbolic links, invalid UTF-8 names,
  duplicate normalized names, CRC failures, and integer or expanded-size
  overflow.
- Verify the bundled archive against build-generated size and SHA-256
  metadata before installation.
- Never buffer the complete archive or complete extracted tree in memory.
- Preserve the previous valid generation until the replacement is fully
  extracted, verified, committed, and published.
- Recover deterministically after interruption at every publication step.
- Require `start.lua` and the complete build-generated required-file set.
- Garbage-collect abandoned staging and superseded generations only after a
  valid active generation has been selected.
- Treat CacheStorage eviction as a reinstall condition, not as loss of user
  state.

State writes and SQLite transactions must use an archive-aware durability
boundary. A successful engine-level durable write is not reported until the
file is closed and the SaveData device has been committed. Multi-file state
publication uses a recoverable journal or dual-slot protocol; deleting a live
destination before rename is not an atomic update strategy.

The libcurl cookie engine is process-wide and shared by every transfer. Its
Netscape cookie jar lives in per-user SaveData, is reloaded before the first
request, and is flushed and committed only when the cookie set changes (plus
shutdown/suspend). Clearing cookies removes both the shared in-memory set and
the durable jar. Cookie, key/value, SQLite, and lifecycle commits are
serialized with one SaveData commit boundary; parallel downloads use the same
serialized boundary for CacheStorage publication.

Writable SQLite databases must use `file://state/`. On Switch those paths
bypass the legacy read-only encrypted-asset VFS, use SQLite DELETE journals
with `synchronous=FULL`, and pass every journal/database sync and deletion
through the SaveData-commit VFS. Read-only encrypted databases under the
installed asset generation retain the original engine VFS and never create a
journal. The small platform key/value store separately keeps a previous
generation until its replacement has been atomically published and committed.

## Account contract

The NACP requests user selection at application launch. Runtime consumes the
preselected account and mounts only that account's SaveData. Cancellation,
invalid user identity, missing archive entitlement, and mount failure are
reported distinctly and fail closed. The user remains fixed for the process
lifetime; changing users requires returning to HOME and relaunching the title.

Two users on one console must have independent credentials, settings, device
identity, cookies, and databases. CacheStorage may contain reproducible shared
content, but no file there may be sufficient to authenticate as a user.

## Lifecycle contract

Applet messages are consumed through registered libnx hooks rather than frame
polling alone. The host distinguishes foreground, library-applet obstruction,
background/HOME, resume, exit request, operation-mode change, and performance-
mode change.

Pause ordering mirrors the proven Android engine contract:

1. cancel active pointers while the input task can still consume cancellation;
2. stop game progression with `pauseGame(true)`;
3. quiesce audio through `IAudioSystem::onActivityPause()`;
4. stop issuing rendering callbacks and cancel pre-suspend network transfers;
5. finish and commit pending durable state.

Resume ordering is:

1. reacquire network, graphics, controller, and audio platform state;
2. resume audio;
3. notify `INotificationManager::onActivityResume()` when present;
4. resume game progression with `pauseGame(false)`.

Exit performs the engine shutdown exactly once while platform services remain
valid. It must not rely on `_Exit` to hide unsafe static destruction.

## Network and download contract

The host creates and owns a NIFM internet request and an explicit
`SocketInitConfig`. Buffer sizes, BSD-session count, transfer concurrency, and
curl connection limits are selected from measured engine demand and recorded
in logs. Defaults are not assumed adequate.

Every transfer records queue delay, DNS time, connect time, first-byte time,
payload bytes, elapsed time, average throughput, retry/reconnect events, write
time, and publication time. Downloads stream into a temporary CacheStorage
file, close and verify it, then publish it without exposing a partial final
file. Suspend or connectivity loss must either resume safely or fail the
engine request promptly; it must never leave the request indefinitely frozen.

## Packaging contract

Packaging consumes `AppAssets.zip` as an external build input and embeds that
unchanged archive plus generated integrity metadata into RomFS. Official
assets and console keys are never committed. NACP generation structurally
sets the title ID, startup account policy, SaveData sizes, CacheStorage sizes,
and supported languages. Raw undocumented byte-offset patching is prohibited.

NPDM filesystem, service, syscall, and debug capabilities are an allowlist
derived from linked libnx facilities and hardware traces. Wildcard service
access and all-filesystem permissions are not accepted in a release package.
The installed title has the narrow filesystem capability mask `0x21`: content
access plus SaveData creation. On first launch the runtime first attempts each
mount, creates only a missing account SaveData or CacheStorage using the same
sizes compiled into the NACP, and retries the mount. Corruption, permissions,
media errors, and every result other than FS `TargetNotFound` fail closed. A
small platform-owned progress display is presented before provisioning and
during extraction; it pumps exit, HOME/focus, resume, and dock/handheld events
without depending on files from AppAssets.

## Acceptance matrix

Host-side and static gates:

- clean configure/build of Linux, Windows, macOS compile-check, all Android
  presets, and Switch;
- deterministic archive metadata and deterministic NRO/NSP staging inputs;
- archive traversal, duplicate-name, CRC, truncation, overflow, interruption,
  rollback, cache-eviction, and upgrade tests;
- path-policy tests covering every engine `file://external`, `file://asset`,
  and `file://install` producer;
- recoverable state-store interruption tests and static SQLite VFS checks;
- NPDM/NACP manifest validation and absence of bundled secrets.

Real hardware gates, performed for both handheld and docked operation where
applicable:

- first install, no-op relaunch, archive upgrade, interrupted extraction, and
  cache eviction/reinstallation;
- two-user isolation and selector cancellation;
- twenty HOME/resume cycles at title, home screen, gameplay, and during a
  download;
- sleep/wake during gameplay, SaveData mutation, and downloads;
- repeated dock/undock, controller disconnect/reconnect, touch cancellation,
  and clean HOME-menu exit;
- offline launch, airplane-mode interruption, access-point loss, and network
  reconnection;
- base package and on-demand asset downloads with throughput compared against
  a standalone transfer on the same console and network;
- clean audio, notification handling, rendering, user-state persistence, and
  no leaked staging generations after every scenario.

The port is complete only when this matrix is recorded as passing on an
installed title. An NRO-only run, successful compilation, or a title-screen
boot is not sufficient.

## Prerequisites

Install the current devkitPro Switch toolchain and portlibs packages for
libnx, SDL-independent EGL/GLES2, curl, libpng, zlib, mbedTLS, and SQLite.
`DEVKITPRO` must name the devkitPro root (normally `/opt/devkitpro`). CMake
3.24 or newer, Ninja, and Python 3 are also required.

Two inputs deliberately remain outside this repository:

- the original `AppAssets.zip` obtained by the user;
- a complete 256x256 JPEG application icon.

The build never extracts the archive into the package. It hashes and embeds
the original bytes. Console keys are required only for the final NSP pack
step; they are never read by CMake, copied into the staging tree, or stored in
the repository.

When the archive is supplied inside the community Android package, extract
the single entry to an external/generated location without unpacking the ZIP
itself, then pass that path to CMake:

```sh
unzip -p /path/lovelive-community.apk assets/AppAssets.zip \
  > /external/generated/AppAssets.zip
sha256sum /external/generated/AppAssets.zip
```

The community archive used by the emulator acceptance run below has SHA-256
`12ba02478978819bd37407eb27bf54ed8b34a93ba814356ddbe2ad4c0c988c20`.

## Development NRO

Configure and build a self-contained NRO with the original archive in RomFS:

```sh
export DEVKITPRO=/opt/devkitpro
cmake --preset switch-release \
  -DPLAYGROUND_APP_ASSETS_ZIP=/absolute/path/AppAssets.zip
cmake --build --preset switch-release --target \
  playground-switch-engine-nro playground-switch-engine-npdm-audit \
  playground-switch-engine-nacp-audit
```

The output is:

```text
out/build/switch-release/Engine/modern/host/switch/playground-switch-engine-nro.nro
```

Copy it to `sdmc:/switch/PlaygroundOSS-SIF/PlaygroundOSS-SIF.nro` and launch
it through the Homebrew Menu. NRO mode is announced as development mode and
uses only the explicit title-scoped directory
`sdmc:/switch/PlaygroundOSS-SIF/{state,cache}`. It cannot exercise account
selection, application SaveData, CacheStorage provisioning, installed-title
capabilities, or clean HOME-menu lifecycle; those require the NSP.

Handheld mode uses the touch screen directly. Docked mode displays a small
pink controller cursor: move it with the left stick or D-pad, press or hold A
for touch-down and drag, release A for touch-up, and use B for Android-
compatible back. Plus requests a clean application exit. Pointer state is
canceled on focus loss and dock/undock so a system applet cannot leave a stuck
touch.

For an NRO built without bundled RomFS, place `AppAssets.zip` in that same SD
directory and generate its adjacent metadata with:

```sh
python3 scripts/generate_appassets_metadata.py \
  /path/AppAssets.zip \
  /path/to/sdmc/switch/PlaygroundOSS-SIF/AppAssets.metadata
```

## Audited installed-package staging

Configure with both external package inputs, then build the audited staging
tree:

```sh
export DEVKITPRO=/opt/devkitpro
cmake --preset switch-release \
  -DPLAYGROUND_APP_ASSETS_ZIP=/absolute/path/AppAssets.zip \
  -DPLAYGROUND_SWITCH_ICON_JPEG=/absolute/path/icon-256x256.jpg
cmake --build --preset switch-release --target \
  playground-switch-package-stage playground-switch-engine-npdm-audit \
  playground-switch-engine-nacp-audit
```

The deterministic input tree is under:

```text
out/build/switch-release/Engine/modern/host/switch/package-stage/
  exefs/main
  exefs/main.npdm
  control/control.nacp
  control/icon_AmericanEnglish.dat
  romfs/AppAssets.zip
  romfs/AppAssets.metadata
  package-manifest.json
```

`package-manifest.json` records the size and SHA-256 of every input. Re-running
the stage target from identical inputs must reproduce every file. The NPDM
audit derives the exact linked SVC set from the ELF and rejects wildcard
services, service hosting, excess filesystem permissions, or an incorrect
title-ID range.

The defaults use title ID `0x0100F59153491000`, 256 MiB per-user SaveData
with a 64 MiB journal, and 8 GiB CacheStorage with a 64 MiB journal. Override
the `PLAYGROUND_SWITCH_TITLE_ID`, `PLAYGROUND_SWITCH_SAVE_*`, and
`PLAYGROUND_SWITCH_CACHE_*` CMake cache values only before distribution;
changing title ID creates a distinct title/storage identity.

Old yuzu 1734 does not implement the modern command-22 layout or
`IFileSystem::RenameDirectory`. The installed runtime does not identify an
emulator by name and does not weaken the hardware path. It first sends the
standard libnx request; only the unique malformed System-save record produced
by yuzu's misordered decoder enables a compatibility request and a streamed
publication of the already validated staging generation. Real Horizon keeps
the normal account SaveData/CacheStorage requests and atomic directory rename.

## Creating and installing the NSP

Create an NSP from the audited stage with a user-provided hacBrewPack binary
and keyset:

```sh
python3 scripts/package_switch_nsp.py \
  --stage out/build/switch-release/Engine/modern/host/switch/package-stage \
  --keys /secure/path/prod.keys \
  --hacbrewpack /path/to/hacBrewPack \
  --output out/package/switch
```

The script rejects incomplete stages and outputs exactly one PFS0 NSP. Install
that NSP using the user's normal title installer. Launch it from the HOME menu
so Horizon performs the required account selection. The runtime opens the
selected account's SaveData and the title's CacheStorage, provisioning either
one from the NACP-backed size policy only when Horizon reports that it does not
exist. A production launch fails closed if either store remains unavailable;
it never falls back to SD storage.

For yuzu 1734, use **File > Install Files to NAND...**, select the generated
NSP, and confirm the installation dialog. Launch the installed title from the
game list; opening an individual NCA is only a developer diagnostic and is not
the distribution workflow. A successful first boot writes
`user/logs/runtime.log` in the title's account SaveData and reaches the title
screen after validating or extracting AppAssets. yuzu 1734's command-22 and
directory-publication compatibility is selected only from its mechanically
identified malformed save record, as described above.

The pack step verifies every staged file against `package-manifest.json`,
rejects symbolic links and undeclared files (including accidental keys), and
gives hacBrewPack a disposable copy because older versions modify the NACP in
place. Packer output is captured because older releases can echo values from
unrecognized keyset entries. The wrapper then verifies a strict three-NCA PFS0
layout (Program, Control, and Meta container), re-verifies the immutable stage,
and atomically publishes only the expected title-ID-named NSP. Stale NSPs are
removed only after successful publication. The keyset remains at the path
supplied by the user and is never copied into either tree.

For an independent release check, build the official `hactool` and verify the
published PFS0 and each extracted NCA using `--verify`, `--disablekeywarns`,
and `--suppresskeys`. The recorded release package was independently checked
this way: all three NCAs report title ID `0100f59153491000`, and all reported
superblock/hash-table checks are good. Keep verifier output private because it
is derived from a user-owned keyset.

## Runtime path and durability rules

Engine URIs have fixed ownership:

- `file://install/` is the active, read-only extracted AppAssets generation;
- `asset://` and `file://asset/` prefer a downloaded CacheStorage override and
  otherwise read the install generation;
- `file://external/` is reproducible downloaded/cache content;
- `file://state/` is private per-user SaveData.

Traversal, absolute, drive-qualified, backslash, and control-character URI
paths are rejected. Cache publications and SaveData publications commit their
respective filesystem devices. On Switch, the default SQLite VFS additionally
commits `pgstate` after successful writable database/journal synchronization,
deletion, and dirty close.

Switch devoptab paths (`pgstate:/...` and `pgcache:/...`) are already absolute.
The Switch SQLite VFS preserves them verbatim instead of allowing the generic
Unix canonicalizer to prepend `/`, which would make encrypted packaged
databases inaccessible.

The engine's legacy encrypted SQLite wrapper remains restricted to logical
asset databases. The selection is made before SQLite opens the file, while the
original URI is still known: `asset://` uses the encrypted VFS whether it
resolves to the bundled generation or a downloaded CacheStorage override;
`file://state/` and `file://external/` use the named Switch durability VFS.
This avoids guessing from physical paths or extensions—the game has both an
encrypted downloaded `game_mater.db_` and a writable plain `unit_list.db_`.
A missing asset with no resolved native path uses the durable temporary-SQLite
fallback. The durability VFS is based on SQLite's `unix-none` backend: Horizon
mounts are private to this application process, while libnx devoptab
intentionally lacks POSIX `fcntl` advisory locks. SQLite still performs
ordinary writes, syncs, journaling, and explicit SaveData commits; only
unavailable cross-process locking is omitted.

HOME/sleep cancels active curl transfers through the transfer progress hook,
then commits state/cache without destroying BSD beneath a running callback.
Each transfer logs queue, DNS, connect, first-byte and total time, payload
bytes, average throughput, connection count, and redirects. Screenshots are
decoded, aspect-preserving scaled to 1280x720 RGBA, and written through the
application album service rather than copied to an SD directory.

The native regression suite starts a local HTTP server and proves cookie
capture, no-op dirty detection, on-disk persistence, process restart/reload,
request replay, and complete clearing. This is part of the ordinary Linux
`ctest` gate and does not require official assets.

Installed-title stdout/stderr is line-buffered into the selected user's
`logs/runtime.log` in SaveData and rotated at 4 MiB to
`runtime.previous.log`. Suspend and orderly exit flush and commit that log.
NRO mode keeps stdio attached to the Homebrew Menu/nxlink console. Release
acceptance logs can therefore be obtained by dumping the test user's SaveData
without placing private diagnostics in shared SD storage.

## Validation commands

Run the portable failure-injection and path-policy checks on a Linux build:

```sh
cmake --preset linux-release
cmake --build --preset linux-release
python3 scripts/test_asset_bootstrap.py \
  out/build/linux-release/Engine/modern/bootstrap/playground-asset-bootstrap-probe
out/build/linux-release/Engine/modern/runtime/playground-runtime-path-policy-probe \
  /tmp/playground-path-policy
```

Run `git diff --check`, rebuild the Switch NRO and NPDM audit, and stage the
package twice to confirm identical manifests before hardware testing. Record
the hardware results against the acceptance matrix above; tests not executed
on real hardware must remain explicitly pending rather than being inferred
from an emulator or NRO run. Use
[SwitchHardwareAcceptance.md](SwitchHardwareAcceptance.md) as the release
record.
