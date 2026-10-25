# Nintendo Switch hardware acceptance record

This record is intentionally separate from compile-time evidence. Fill it in
for the exact NSP manifest and console firmware used for release. `PASS` means
the observed behavior and logs were reviewed; a boot, NRO run, or emulator run
does not substitute for an installed-title result.

## Build identity

### V7 hardware result and subsequent download test, 2026-09-19

Screenshots `2026091913551600` and `2026091913552300` show the v7 probe passed
its suite and separate-launch database/publication persistence. The user also
reports that the game boots without the previous database errors. This is not
blanket acceptance of every database transaction/lifecycle scenario.

Screenshot `2026091914023600` shows a generic download failure. The overlapping
CacheStorage-writer failure was reproduced locally and a coordinator added;
see `SwitchCacheWriterCoordination.md`. Its hardware acceptance is pending.

### Shared-handle/root correction, 2026-09-19 (physical test pending)

Game package `out/package/switch-game-shared-handles/0100f59153491000.nsp`, SHA-256
`e6d8406499f014d7aef543b9fc474e0e23c7d75d853604c5287e83c4f13519db`.
Matching ELFs are retained alongside both the game and v7 probe packages.

Game screenshot `2026091913421400` identifies an incorrect `pgstate://` prefix
check. Probe v6 screenshots `2026091913403100` / `2026091913403900` independently
identify TargetLocked on the second native writable handle to one file. Both
are corrected generically in the Switch adapter; the v7 probe adds regression
coverage and a fresh run marker. No game saves or caches are cleared. Local
tests/builds do not constitute physical acceptance; nothing is approved to push.

### Isolated database probe evidence, 2026-09-19

Probe v4 (`0100F59153492000`, not the game title) passed its implemented suite
on physical hardware and passed encrypted-database persistence on the next
launch. Evidence: screenshots `2026091913072000` and `2026091913073000`;
NSP SHA-256 `31f3538dd9a622ec3eb485997f7e77e0eb69a2c7116ee162b91860ad81485c02`.
See `SwitchDatabaseProbe.md` for scope and remaining gates. No game-release
status below is promoted by this isolated result, and console pushes remain
unapproved.

### Game build (not the isolated probe)

The initial boot test reached a Lua database-open assertion (screenshot
`2026091913305800`); the old assertion omitted the underlying SQLite error.
Current diagnostic game package:
`out/package/switch-game-open-diagnostics/0100f59153491000.nsp`, SHA-256
`22a919a77811a5ff421e853c456c12dee47168700598acb44613a40ac0d0e1cc`.
It reports SQLite primary/extended codes and captured native open/create or
header-validation errors. Native errors are captured before cleanup, not read
later from stale errno. Device behavior is pending; no existing saves cleared.

Current exploratory boot package: `out/package/switch-game-boot-20260919/0100f59153491000.nsp`,
SHA-256 `fa31e9a42617e3ad7f92c873f53ab96b37d3f3f278ccd0afb74a6da5524e8858`.
The game host now registers the tested adapter and routes SaveData callbacks
through it. A local worker-commit/read serialization regression passes, and
the Web engine rebuild and Switch packaging audits pass. Existing saves and
assets are not cleared. Physical boot and full game behavior are PENDING;
this is not a release approval.

Probe v5 also passed on physical hardware: screenshots `2026091913192000`
and `2026091913193100` show suite success and next-launch persistence for both
the encrypted database and non-database publication. NSP SHA-256:
`cfd5e15c0da024894c6f197b2bf5d9976c4a2b15c4a0e895569f4ebf61e3efb0`.
This includes the tested external-commit callback coordination, not actual
sleep/resume or network-worker cancellation. Production host wiring is pending.

| Field | Value |
| --- | --- |
| Git commit | PENDING |
| `package-manifest.json` SHA-256 | `c5aedeb2054ffcfaedf985ce802b839db12b9fccdfb714761d1d46dc32f947bc` |
| NSP SHA-256 | `be915e7dbc47dea651dd4bb08e2831a026ea7b2e9d379bab223f9693f6ce5b0f` |
| Title ID | `0x0100F59153491000` |
| Console firmware | PENDING |
| Atmosphère/version | PENDING |
| AppAssets SHA-256 | `12ba02478978819bd37407eb27bf54ed8b34a93ba814356ddbe2ad4c0c988c20` |

## Installation and storage

| Test | Handheld | Docked | Evidence/notes |
| --- | --- | --- | --- |
| Fresh install selects a user and creates private SaveData | BLOCKED | N/A | Database integration was removed after repeated physical-hardware failure; implement the approved adapter first. |
| First boot verifies/extracts the bundled ZIP | PENDING | PENDING | Hardware previously completed extraction; rerun against the audited package is required. |
| Relaunch validates existing generation without extraction | PENDING | PENDING | Rerun against the audited package is required. |
| Interrupted extraction retains previous valid generation | PENDING | PENDING | |
| Archive upgrade publishes new generation then removes old | PENDING | PENDING | |
| CacheStorage eviction triggers clean reinstall, preserving user state | PENDING | PENDING | |
| Two accounts have distinct identity/credentials/preferences/mutable databases | BLOCKED | BLOCKED | Requires the isolated database adapter and a fresh two-account hardware run. |
| Login cookies survive relaunch, clear completely, and remain user-private | PENDING | PENDING | |
| User-selector cancellation fails closed | PENDING | N/A | |
| No production data appears under `sdmc:/switch/PlaygroundOSS-SIF` | PENDING | N/A | |

### Disposable fresh-storage audit title

This v2 package is retained only as rejected evidence. Its distinct title ID
prevented older experimental storage from hiding first-boot faults, but
physical hardware proved that its raw 70-byte archive component cannot be
created in CacheStorage. Do not install it as the next test package.

| Field | Value |
| --- | --- |
| `package-manifest.json` SHA-256 | `38901f09db4669c93da34632bb170aa1d4141e57acdc0be1b2b46dc2a266c776` |
| NSP SHA-256 | `cc7cd70c70414c2628a0dcdaf8f2bc835078d85e88ccfec7f4c847ed6147c84b` |
| Title ID | `0x0100F59153494000` |
| NSP | `out/package/switch-final-fresh-audit-v2/0100f59153494000.nsp` |
| Purpose | REJECTED: exposed CacheStorage's 64-byte physical component limit at a specific 70-byte logical asset name. |

### Rejected v3 physical-hardware candidate

This package superseded v2 for one physical-hardware test. It successfully
exercised the encoded CacheStorage extraction path, but is rejected: engine
startup still asserts while opening `file://external/client_settings.db`.
Static path and package audits did not establish the end-to-end SQLite/VFS
contract and must not be treated as hardware acceptance.

| Field | Value |
| --- | --- |
| `package-manifest.json` SHA-256 | `c0d471fb1a6b686488e938ce744ea30c7d5c1b0cc8b8cfb5d955a4a78c2e5f21` |
| NSP SHA-256 | `d04f989bd43595bbc6b70b1da5a451243bb3cf64c499e13769de37ebc5c54fa4` |
| Title ID | `0x0100F59153494000` |
| NSP | `out/package/switch-final-fresh-audit-v3/0100f59153494000.nsp` |
| Purpose | REJECTED: extraction progressed, but `client_settings.db` could not be opened on physical hardware. |

## Lifecycle, graphics, input, and audio

Run twenty cycles at title, home screen, gameplay, and active download.

| Test | Handheld | Docked | Evidence/notes |
| --- | --- | --- | --- |
| HOME/resume x20 at each required scene | PENDING | PENDING | |
| Sleep/wake during gameplay | PENDING | PENDING | |
| Sleep/wake during SaveData mutation | PENDING | PENDING | |
| Interrupt/relaunch during SQLite writes and SaveData commit | BLOCKED | BLOCKED | Database integration is intentionally absent. |
| Sleep/wake during download returns/cancels promptly | PENDING | PENDING | |
| Dock/undock repeatedly recreates surface and viewport | PENDING | PENDING | |
| Touch cancellation on focus loss has no stuck pointers | PENDING | N/A | |
| Controller disconnect/reconnect and B/back behavior | PENDING | PENDING | |
| Audio remains clean across pause/resume and route changes | PENDING | PENDING | |
| Screenshot reaches the system Album with correct orientation | PENDING | PENDING | |
| Clean HOME-menu exit runs engine shutdown exactly once | PENDING | PENDING | |

## Network and content

Preserve the `network metrics`, `asset download write`, and `asset download
publication` lines for each case.

| Test | Handheld | Docked | Evidence/notes |
| --- | --- | --- | --- |
| Offline launch reports a usable error and does not freeze | PENDING | PENDING | |
| Airplane mode during transfer fails promptly | PENDING | PENDING | |
| Access-point loss/recovery allows a safe retry | PENDING | PENDING | |
| Initial package download and atomic publication | PENDING | PENDING | |
| On-demand asset download and immediate override read | PENDING | PENDING | |
| Throughput is compared with a standalone console transfer | PENDING | PENDING | |
| No partial final file or abandoned staging generation remains | PENDING | PENDING | |

## Release decision

- [ ] All required rows are `PASS`.
- [x] NPDM and NACP audits pass for the final ELF/package.
- [x] A second staging run is byte-identical.
- [ ] The final NSP is byte-identical across two package runs. hacBrewPack
      3.05 randomized this uncommitted hardware-test package; the audited
      staging manifest remains deterministic.
- [x] Independent hactool verification reports valid hashes for the Program,
      Control, and Meta NCAs. The production package reports title ID
      `0100f59153491000`; the disposable clean-title package reports
      `0100f59153494000`. Every v3 reported superblock, hash-table, and RomFS
      IVFC level is `GOOD`. The zeroed fixed-key/NPDM signature fields are
      expected for these unsigned homebrew packages and are not content-hash
      failures.
- [x] No official assets, keys, generated NCA, or NSP are committed.
- [ ] Any failure has a linked issue and the release remains blocked.

## Audit correction and emulator evidence

The hardware-test package recorded above corrects installed-title service
contracts that yuzu 1734 did not enforce: application processes request
`nvdrv` (not the applet-only `nvdrv:a`), the runtime explicitly acquires and
releases `csrng`, and libnx can open `set:sys` during process initialization to
discover the real Horizon version before using version-gated CacheStorage.
The NPDM audit derives all three requirements from linked libnx clients.
Physical-console boot remains required before these changes are accepted as
hardware evidence.

Physical hardware additionally exposed FS 2-6706 (`MappingTableFull`) after
2,757 files had been created in one uncommitted CacheStorage transaction.
Installed-title extraction now commits its deliberately incomplete staging
generation every 256 archive entries. The completion marker remains the final
write, so an interruption cannot validate a partial generation; the periodic
commits only bound Horizon's journal mapping-table usage.

The fresh-title run dated 2026-08-22 then stopped while removing the old
`.staging-<sha256>` tree with FS 2-6003 (`TooLongPath`). That package is
rejected. The replacement keeps the immutable generation's complete SHA-256
as its 64-character basename and uses `.staging` for the bootstrap-owned
workspace. If a previously interrupted staging tree cannot be traversed and
there is no completed generation to preserve, the runtime recreates only the
title's replaceable CacheStorage and retries; account SaveData is not involved.

The first successful hardware extraction exposed a database/storage ownership
error. Subsequent attempts added raw and encrypted VFS variants, open-mode
routing through `IPlatformRequest`, copy-up, synthetic file metadata,
filename and slash-depth classifiers, `:memory:` fallback, and database-close
archive commits. These changes repeatedly moved the failure without producing
a physically accepted implementation. The last package still failed to open
`file://external/client_settings.db`.

That experiment is now rejected in full. The cleanup was checked against the
pre-Switch baseline `f56fe3a4977988053b76765b1bfe5c2617b0873c`: Switch
database source files, host initialization and shutdown, common-engine Switch
branches, the database-specific `IPlatformRequest` API, RuntimePlatform
database routing/copy-up, and assertions that claimed the design worked have
been removed. Android's established writable-path flag behavior is restored.

The 2026-09-19 candidate adapter is currently linked only into the standalone
[database probe](SwitchDatabaseProbe.md), not the game host. Compilation
or a title-screen boot must not be reported as database support. The
implementation must follow
[SwitchDatabaseArchitectureProposal.md](SwitchDatabaseArchitectureProposal.md),
remain game-agnostic, live in the Switch runtime directory, and pass a
synthetic encrypted-database conformance program on physical hardware before
the game is used as an acceptance test.

The audit also removed two quieter uses of desktop assumptions. Cache free
space is queried through Horizon's native filesystem service rather than
POSIX `filesystem::space`, and suspend now waits for network transfer leases
to quiesce before committing CacheStorage instead of racing an in-progress
temporary-file write.

The final platform audit also verifies NIFM's request result after submission
rather than mistaking successful IPC for network availability. Alarm
replacement, cancellation, and expiry are scoped to the launch-selected
account and update an existing tag without a delete/register gap. Controller
disconnect cancels a held synthetic touch, and six-axis input reacquires a
handle after controller disconnect or Npad style changes.

The non-database external namespace still behaves as one filesystem even
though Horizon requires two storage classes. Logical delete and rename clean
obsolete lower copies so data cannot reappear. Durable engine state uses the
explicit state namespace. Unrecoverable key/value-state corruption fails
startup instead of silently replacing the account's device identity and
credentials.

The v2 fresh-title run supplied the missing physical evidence: extraction
failed on
`atlas-gui-idol-skill-idol-skill-module-icon-event_blank-front.png.imag`,
whose UTF-8 component is exactly 70 bytes, with FS 2-6003 (`TooLongPath`). The
complete native path was only 120 bytes, so the failure is an individual
CacheStorage component limit, not the `0x300` full-path limit. The archive has
8,462 entries, a maximum logical component of 70 bytes, and a maximum relative
path of 98 bytes.

Installed-title storage now applies one lossless filesystem codec at the
logical/native boundary. A component of at most 64 bytes remains unchanged;
an oversized component is base64url encoded and split across private,
reserved `~pg*` components of at most 64 bytes. Literal logical names using
that prefix are escaped too, making the mapping injective. Directory listings
decode the representation, so the engine and game retain their exact logical
names. The mapper is applied to bootstrap extraction, validation, runtime
reads/writes, downloads, rename, removal, and merged enumeration;
NRO/SD mode remains unencoded. The complete supplied archive has been checked
for collisions and maps to a maximum 64-byte physical component and 161-byte
physical path. This is distinct from the independently required periodic
commits for FS 2-6706 (`MappingTableFull`).

Old yuzu 1734 compatibility was also removed from production. Its field-order
bug had led the title to issue intentionally malformed FS IPC and use a second,
copy-based publication protocol. Current emulators should implement standard
libnx requests; physical hardware remains the authority for every row above.
