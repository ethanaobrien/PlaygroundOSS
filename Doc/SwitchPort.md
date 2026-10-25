# Nintendo Switch/libnx port

This document is the implementation contract for the clean libnx port. The
port starts from the `modernization` branch. Earlier experimental Switch
commits are evidence that the engine can run on the hardware, but their source
and commit history are not imported into this branch.

## Goal

Deliver a production-quality Nintendo Switch port as an installed, user-
selecting application title: bundle the original `AppAssets.zip` unchanged;
extract, validate, upgrade, and recover it transactionally in CacheStorage;
keep durable identity, credentials, preferences, and explicitly state-owned
application data in the launch user's SaveData; keep replaceable downloaded
content in CacheStorage regardless of its file format; implement complete
libnx graphics, input, audio, network,
lifecycle, notification, and system-service behavior; package with audited
NACP/NPDM least privilege; and pass both reproducible host-side gates and the
recorded real-hardware acceptance matrix. The development NRO may use only an
explicit title-scoped SD directory and is never accepted as production proof.

## Product and development modes

The production artifact is an installed application title. It must have a
stable, configurable application ID, a launch-selected user account, account
SaveData, application CacheStorage, and an immutable RomFS. Failure to acquire
any required production mount is a startup error. Production must never place
credentials, application data, extracted application files, or downloads in
an unscoped `sdmc:` directory.

The NRO is a development artifact. Because a homebrew NRO has no application
SaveData or CacheStorage of its own, it may use an explicitly selected,
title-namespaced SD directory. The program must identify that mode in its log
and UI. Development fallback is not shared with the installed title and must
not be selected automatically by the production executable.

## Storage contract

The engine sees three roots. Ownership is never inferred from a filename,
extension, path shape, caller, or game identity. `file://state/` is explicit
durable state. The legacy `file://external/` namespace is presented as the
single app-private filesystem mobile clients expect, backed by an account
SaveData upper layer and a replaceable CacheStorage lower layer.

| Root | Backing storage | Contents |
| --- | --- | --- |
| `install` | CacheStorage | Transactionally extracted `AppAssets.zip` |
| `content` | CacheStorage | Downloaded packages, temporary downloads, and other reproducible assets |
| `state` | Launch user's account SaveData | Credentials, device identity, preferences, cookies, and explicit durable state |

The package contains the original `AppAssets.zip` as one RomFS file. It does
not contain an extracted AppAssets tree. On first launch or archive upgrade,
the bootstrap streams the archive into a staging generation under
CacheStorage, validates every entry and required engine files, commits the
generation, and atomically renames it to the immutable directory selected by
the complete 64-character bundled SHA-256. On constrained Horizon storage the
temporary workspace is the fixed bootstrap-owned `.staging` directory; it is
never valid until publication. The bundled hash is the active-generation selector,
so there is no separately mutable active pointer to tear.

CacheStorage on physical hardware rejects an individual filename component
longer than 64 bytes even when the complete path is well below Horizon's
`0x300` path bound. Installed-title mode therefore uses a lossless physical
filesystem encoding. Logical components up to 64 UTF-8 bytes remain unchanged;
oversized components (and literal names in the private `~pg*` namespace) are
base64url encoded and split into reserved physical components of at most 64
bytes. All engine-facing paths and directory listings remain the original
logical names. The codec is generic, injective, and applied once at the
filesystem boundary across bootstrap and runtime operations; it contains no
application asset names. Development NRO storage on SD is not encoded.

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

State writes must use an archive-aware durability boundary. Engine-level state
publication commits the SaveData device after the completed file operation and
at lifecycle boundaries. Multi-file state
publication uses a recoverable journal or dual-slot protocol; deleting a live
destination before rename is not an atomic update strategy.

The libcurl cookie engine is process-wide and shared by every transfer. Its
Netscape cookie jar lives in per-user SaveData, is reloaded before the first
request, and is flushed and committed only when the cookie set changes (plus
shutdown/suspend). Clearing cookies removes both the shared in-memory set and
the durable jar. Cookie, key/value, and lifecycle commits are
serialized with one SaveData commit boundary; parallel downloads use the same
serialized boundary for CacheStorage publication.

New engine-owned writable state uses `file://state/`. Ordinary external
downloads remain in CacheStorage. Directory enumeration merges the state and
content layers, rename preserves the source layer, and logical deletion
removes every physical layer of that object so an obsolete lower copy cannot
reappear.

Switch database integration is intentionally absent. The rejected
implementation passed database-specific open modes and commit callbacks
through `IPlatformRequest`, added Switch branches to the shared database
engine, and still failed repeatedly on physical hardware. Its code and claims
have been removed. [SwitchDatabaseArchitectureProposal.md](SwitchDatabaseArchitectureProposal.md)
defines the replacement gate; until that design is implemented and accepted
on real hardware, database-dependent Switch startup remains blocked. The
2026-09-19 candidate is available only in the isolated
[database conformance probe](SwitchDatabaseProbe.md); the game host does not
register it yet.

The separate `PLAYGROUND_APP_INTEGRITY_DATABASE_URI` build setting belongs to
the application protocol adapter. The Switch host passes it into the generic
runtime; neither Switch storage nor `RuntimePlatform.cpp` names that database,
and the engine default is empty. The application packaging configuration must
provide a URI when its server protocol requires one.

The legacy `ENGINE_USE_DEFAULTDB` feature is likewise identity-free. The
engine no longer embeds a `gamedb.db` path; an application that enables the
flag must first call `CKLBGameApplication::setDefaultDatabaseUri()`. Enabling
the feature without an application URI fails startup instead of guessing a
game-owned filename.

The per-user key/value state store fails closed if both its active and recovery
generations are malformed or unreadable. It does not manufacture a new device
identity over corrupt SaveData. Startup reports that storage failure before
initializing the game or audio runtime.

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

The network lease layer waits up to ten seconds for canceled libcurl workers
to release CacheStorage. SaveData is still committed if that bound is reached,
but CacheStorage is deliberately not committed concurrently with a remaining
temporary-file writer; the timeout is logged as a lifecycle failure.

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
- recoverable state-store interruption tests and a fail-closed audit proving
  that the rejected Switch database adapter is absent;
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

## Runtime-platform audit (2026-08-22)

The complete Switch boundary was re-audited after hardware repeatedly exposed
an empty writable database. The review compared the implementation with the
pre-Switch baseline `f56fe3a4977988053b76765b1bfe5c2617b0873c`, Android, the
earlier hardware-working draft, libnx service ownership, linked symbols, and
every engine-facing platform method. The database experiment was rejected and
removed rather than described as fixed:

| Area | Incorrect/overcomplicated behavior removed | Audited behavior |
| --- | --- | --- |
| SQLite/database routing | Multiple incompatible VFS wrappers, `:memory:` fallback, filename and slash-depth guesses, hard-coded game databases, copy-up, and archive commits were tried in shared code. None passed physical hardware. | All Switch-specific database files, host registration, shared database-routing APIs, common-engine Switch branches, and database-specific probe claims are removed. The port is explicitly blocked pending the isolated adapter design. |
| Storage ownership | Mapping all external data to one archive either shared user state or misplaced reproducible downloads. | The non-database state/content overlay remains generic. Database ownership will be supplied only by the scoped database adapter, never by `RuntimePlatform`, filename, extension, caller, or game identity. |
| Persistent identity | Corrupt active and recovery state generations were treated like first launch, allowing a new device identity to overwrite damaged user state; recovery publication commit failures were ignored. | Corruption and failed recovery publication stop startup. The runtime never invents replacement credentials or identity over unreadable SaveData. |
| Asset publication | An old-emulator fingerprint selected malformed FS IPC and a copy-based publication protocol. Some stale-tree deletion bypassed the native cleanup hook. | Production uses ordinary libnx provisioning, transactional staging, native recursive deletion, periodic bounded CacheStorage commits, and atomic directory publication only. |
| Physical paths | A hash/truncation experiment was not reversible and coupled only some call sites to a renamed asset namespace. Removing all translation was also wrong: the v2 hardware run failed with FS 2-6003 on an exact 70-byte component while its full native path was only 120 bytes. Bootstrap also previously used 72/73-byte generation/staging basenames, and the state publisher attempted unsupported directory descriptor operations. | Engine/game paths remain exact logical names. Installed storage uses one reversible base64url component codec at every filesystem boundary, splitting only components that exceed the hardware-proven 64-byte limit or collide with its reserved `~pg*` namespace. Listings decode names; rename/removal hide empty continuation spines. The 8,462-entry AppAssets archive maps injectively with max component 64 and max path 161. Bootstrap-owned names use the full 64-character SHA-256 plus `.staging`; authenticated recovery may reset only incomplete replaceable CacheStorage when no completed generation exists. NRO paths remain native SD paths, and no unsupported directory descriptor or permission call is made. |
| Lifecycle/network | HOME suspend could commit CacheStorage while a canceled curl worker still owned a temporary writer; lazy transfer initialization could clear the suspend flag; final shutdown also committed/unmounted storage and destroyed process-wide curl state after a bounded timeout even though a worker could still be executing a platform member. NIFM request submission was mistaken for successful network availability. | HOME uses a bounded cancellation wait and skips CacheStorage commit on timeout. Final teardown has no unsafe timeout: it drains every transfer lease before cookie flush, curl-global cleanup, platform destruction, storage commit, or filesystem unmount. Only explicit resume clears suspension, and the submitted NIFM request's actual result is validated. |
| Input/widgets | The software keyboard opened when a text control was created, Switch did not render the static control/value, and one pointer's widget capture swallowed every other pointer. | Touch/controller hit testing opens the bounded system keyboard only after selection; accepted UTF-8 text returns through the engine callback, the GLES overlay renders the current value, and capture is tracked per pointer. |
| Audio | Plain cross-thread flags and teardown ordering could leave the audio callback or mixer waiting on each other during suspend/shutdown. | Switch-only atomic coordination stops/joins AudioOut before destroying the mixer and fills stale callbacks with silence. |
| System integration | Free space used POSIX emulation, locale was fixed to Japan, direct gyro calls reported no data, notification permission used the wrong callback and alarms were incorrectly declared unsupported, and desktop product identity leaked into integrity data. Alarm enumeration also allowed one selected user to modify another user's same-tag registration. | Native Horizon free-space, language/region, reconnecting motion, memory, Album, sleep, browser, application alarms, and application identity paths are used. Alarm replacement/cancellation/expiry is scoped to the launch-selected account and updates in place. Horizon only exposes weekly schedules, so representable one-shot delays carry an absolute due time and are retired on alarm launch or later manual resume; delays of a week or more fail explicitly instead of firing early or recurring forever. Unavailable ads, location, and clipboard report that limitation instead of fabricating persistence. |
| Packaging | Service auditing covered only a small hand-picked subset and allowed stale ACL entries. | The final ELF must account exactly for all 19 linked libnx services, all linked SVCs, filesystem mask `0x21`, selected-user NACP policy, and configured SaveData/CacheStorage sizes. |

No emulator-specific production path remains. Compile, host probes, descriptor
audits, deterministic staging, and NSP construction are useful evidence, but
the port cannot pass its runtime audit or hardware acceptance until the
database proposal is implemented. The rows in `SwitchHardwareAcceptance.md`
remain the authority for physical behavior.

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
  -DPLAYGROUND_APP_ASSETS_ZIP=/absolute/path/AppAssets.zip \
  -DPLAYGROUND_APP_INTEGRITY_DATABASE_URI=asset://db/unit/unit.db_
cmake --build --preset switch-release --target \
  playground-switch-engine-nro playground-switch-engine-npdm-audit \
  playground-switch-engine-nacp-audit playground-switch-engine-runtime-audit
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
canceled on focus loss, dock/undock, and controller disconnect so a system
applet or removed controller cannot leave a stuck touch. Six-axis input also
reacquires its handle after controller disconnect or Npad style changes.

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
  -DPLAYGROUND_APP_INTEGRITY_DATABASE_URI=asset://db/unit/unit.db_ \
  -DPLAYGROUND_SWITCH_ICON_JPEG=/absolute/path/icon-256x256.jpg
cmake --build --preset switch-release --target \
  playground-switch-package-stage playground-switch-engine-npdm-audit \
  playground-switch-engine-nacp-audit playground-switch-engine-runtime-audit
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

The production runtime contains no emulator-specific IPC or filesystem path.
In particular, old yuzu 1734's misordered command-22 request layout and missing
`IFileSystem::RenameDirectory` are not worked around by issuing malformed IPC
from the title. Test with current emulator releases that implement the libnx/
Horizon contracts, and use physical hardware for release acceptance.

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

For an emulator, use its installed-title workflow, select the generated NSP,
and launch the title from the game list. Opening one NCA is only a developer
diagnostic and is not the distribution workflow. Emulator success is useful
debug evidence but never substitutes for the hardware matrix.

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

Engine URIs have explicit namespace and access semantics:

- `file://install/` is the active, read-only extracted AppAssets generation;
- `asset://` and `file://asset/` prefer a downloaded CacheStorage override and
  otherwise read the install generation;
- new ordinary `file://external/` downloads are reproducible CacheStorage
  content; reads see a selected-account SaveData overlay first;
- `file://state/` is private per-user SaveData.

Traversal, absolute, drive-qualified, backslash, and control-character URI
paths are rejected. Cache publications and SaveData publications commit their
respective filesystem devices.

In installed-title mode those absolute paths are the physical result of the
lossless component codec described above. URI joining always happens in the
logical namespace, after which the complete path is encoded exactly once.
Bootstrap returns a logical install root, not an already encoded path. Runtime
directory enumeration reverses the codec before names reach engine callers.

Database URI resolution, encryption, SQLite VFS selection, write ownership,
and durability are deliberately unspecified here until the isolated database
adapter is implemented. The accepted design is recorded separately so this
platform document cannot accidentally promote another untested workaround to
an implementation contract.

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

Installed-title startup never depends on diagnostic log persistence: Horizon
SaveData stdio redirection is not portable across supported firmware and
libnx combinations. NRO mode keeps stdio attached to the Homebrew Menu/nxlink
console and is the supported verbose-diagnostic path. User state and cache
durability remain explicitly committed independently of logging.

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
