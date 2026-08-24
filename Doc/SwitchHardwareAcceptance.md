# Nintendo Switch hardware acceptance record

This record is intentionally separate from compile-time evidence. Fill it in
for the exact NSP manifest and console firmware used for release. `PASS` means
the observed behavior and logs were reviewed; a boot, NRO run, or emulator run
does not substitute for an installed-title result.

## Build identity

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
| Fresh install selects a user and creates private SaveData | PENDING | N/A | Runtime audit replaced the incorrect writable-database VFS; fresh hardware rerun required. |
| First boot verifies/extracts the bundled ZIP | PENDING | PENDING | Hardware previously completed extraction; rerun against the audited package is required. |
| Relaunch validates existing generation without extraction | PENDING | PENDING | Rerun against the audited package is required. |
| Interrupted extraction retains previous valid generation | PENDING | PENDING | |
| Archive upgrade publishes new generation then removes old | PENDING | PENDING | |
| CacheStorage eviction triggers clean reinstall, preserving user state | PENDING | PENDING | |
| Two accounts have distinct identity/credentials/preferences/mutable databases | PENDING | PENDING | Replaceable read-only content may be shared; writable databases must copy up independently. |
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
| Interrupt/relaunch during SQLite writes and SaveData commit | PENDING | PENDING | |
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

The first successful hardware extraction exposed a path-ownership error. An
intermediate fix inferred ownership from slash depth, which was not a platform
contract; mapping every external database to CacheStorage then shared mutable
user databases across accounts. The audited runtime now uses explicit database
open intent and a generic overlay: `file://state/` maps directly to account
SaveData, ordinary new external downloads use CacheStorage, and writable
external databases copy up into account SaveData. Reads and directory listings
observe the upper layer first. The Switch runtime contains no game database
names, extension, call-site, or slash-depth classifier.

The earlier modernization had also changed Switch and Wii U alone from the
engine's established `journal_mode=OFF` policy to `DELETE/FULL`, creating an
auxiliary-file path that neither Android nor the known-working Switch build
used. That divergence was removed. A later attempt to compensate for observed
devoptab `fstat()` failures wrapped every SQLite file operation and maintained
a synthetic file size. That was another unproven divergence and still produced
empty first-boot databases on hardware. It has also been removed.

The Switch now uses the same actual I/O seam as the earlier working draft:
`unix-none` is cloned only to preserve devoptab paths, then the engine's
encrypted file wrapper becomes SQLite's default VFS. The discarded
implementation instead selected a named raw Unix VFS for writable databases;
that was not equivalent, reached unsupported Horizon descriptor operations,
and caused the repeated empty `basic_setting` database failure. Dirty
databases now report short-write/flush/commit failures to SQLite and ask the
platform to make their resolved path durable at sync/close boundaries. The
generic database engine contains no Switch mount names or storage selection.
An explicitly unnamed temporary database uses SQLite's connection-local
`:memory:` behavior. A named packaged database must resolve to an existing
asset or its open fails; it can no longer become a valid but empty temporary
database and hide a packaging/storage defect. The encrypted VFS also rejects
failed seeks and file-size queries and no longer advertises power-safe sector
overwrite semantics that libnx does not guarantee.

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

The external namespace behaves as one filesystem even though Horizon requires
two storage classes. A writable database open copies an existing CacheStorage
object into SaveData before mutation; read-only opens see the same upper copy.
Logical delete and rename also clean obsolete lower copies so data cannot
reappear. Durable engine state uses the explicit state namespace.
Unrecoverable key/value-state corruption now fails startup instead of silently
replacing the account's device identity and credentials.

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
reads/writes, downloads, databases, rename, removal, and merged enumeration;
NRO/SD mode remains unencoded. The complete supplied archive has been checked
for collisions and maps to a maximum 64-byte physical component and 161-byte
physical path. This is distinct from the independently required periodic
commits for FS 2-6706 (`MappingTableFull`).

Old yuzu 1734 compatibility was also removed from production. Its field-order
bug had led the title to issue intentionally malformed FS IPC and use a second,
copy-based publication protocol. Current emulators should implement standard
libnx requests; physical hardware remains the authority for every row above.
