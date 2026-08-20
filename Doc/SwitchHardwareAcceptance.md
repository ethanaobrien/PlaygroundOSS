# Nintendo Switch hardware acceptance record

This record is intentionally separate from compile-time evidence. Fill it in
for the exact NSP manifest and console firmware used for release. `PASS` means
the observed behavior and logs were reviewed; a boot, NRO run, or emulator run
does not substitute for an installed-title result.

## Build identity

| Field | Value |
| --- | --- |
| Git commit | PENDING |
| `package-manifest.json` SHA-256 | `68cb036d4a349f23a06f175957134359f6fd2a675ceafa6944bdd60555d51078` |
| NSP SHA-256 | `21a114239c444acd434099fc83a7df60edc737906314a7bd2bf52292c3e27273` |
| Title ID | `0x0100F59153491000` |
| Console firmware | PENDING |
| Atmosphère/version | PENDING |
| AppAssets SHA-256 | `12ba02478978819bd37407eb27bf54ed8b34a93ba814356ddbe2ad4c0c988c20` |

## Installation and storage

| Test | Handheld | Docked | Evidence/notes |
| --- | --- | --- | --- |
| Fresh install selects a user and creates private SaveData | PENDING | N/A | yuzu 1734 verification passed with the emulator-selected account; hardware rerun required. |
| First boot verifies/extracts the bundled ZIP | PENDING | PENDING | yuzu 1734 installed all 8,462 files from the community APK archive with lifecycle-aware progress; hardware rerun required. |
| Relaunch validates existing generation without extraction | PENDING | PENDING | yuzu 1734 logged `AppAssets validated` with no extraction progress; hardware rerun required. |
| Interrupted extraction retains previous valid generation | PENDING | PENDING | |
| Archive upgrade publishes new generation then removes old | PENDING | PENDING | |
| CacheStorage eviction triggers clean reinstall, preserving user state | PENDING | PENDING | |
| Two accounts have distinct identity/credentials/preferences/databases | PENDING | PENDING | |
| Login cookies survive relaunch, clear completely, and remain user-private | PENDING | PENDING | |
| User-selector cancellation fails closed | PENDING | N/A | |
| No production data appears under `sdmc:/switch/PlaygroundOSS-SIF` | PENDING | N/A | |

## Lifecycle, graphics, input, and audio

Run twenty cycles at title, home screen, gameplay, and active download.

| Test | Handheld | Docked | Evidence/notes |
| --- | --- | --- | --- |
| HOME/resume x20 at each required scene | PENDING | PENDING | |
| Sleep/wake during gameplay | PENDING | PENDING | |
| Sleep/wake during SaveData mutation | PENDING | PENDING | |
| Interrupt/relaunch during SQLite journal and database sync | PENDING | PENDING | |
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
- [x] The final NSP is byte-identical across two package runs.
- [x] Independent hactool verification reports valid hashes for the Program,
      Control, and Meta NCAs, all with title ID `0100f59153491000`.
- [x] No official assets, keys, generated NCA, or NSP are committed.
- [ ] Any failure has a linked issue and the release remains blocked.

## Emulator compatibility evidence

yuzu 1734 is useful diagnostic evidence but does not replace any hardware row
above. Its obsolete command-22 decoder reads the Horizon/libnx SaveData request
fields in a different order, and its `IFileSystem` omits `RenameDirectory`.
The runtime enables a narrowly gated adapter only after finding the unique
malformed SaveData record produced by that decoder. Normal Horizon continues
to use the standard libnx create request and atomic directory rename.

With the package identity recorded above, yuzu 1734 completed these checks:

- provisioned and mounted account SaveData for the emulator-selected user;
- provisioned and mounted application CacheStorage;
- verified the unchanged bundled archive and published an 8,462-file,
  content-addressed generation using streamed file operations;
- relaunched against that generation without extracting it again;
- preserved `pgcache:/`/`pgstate:/` devoptab paths through SQLite's VFS
  canonicalization;
- kept logical `asset://` databases behind the legacy encrypted asset VFS,
  whether they resolve to the bundled generation or a downloaded override,
  while routing logical writable state/cache databases through SQLite's
  rollback-journal VFS;
- created and populated the first-boot cache databases rather than leaving
  zero-byte files;
- handled a package-absent asset with the same null native-path contract as
  Android, allowing the engine's temporary-database fallback to run; and
- connected to the private server, downloaded and decrypted its updated asset
  databases, and reached the interactive member-selection screen at 32 FPS
  with complete dialog text and no fatal assertion, Lua runtime error,
  `SQLITE_NOTADB`, or SQLite VFS error in `runtime.log`.

The final emulator launch used the Program NCA extracted from the deterministic
NSP whose SHA-256 is recorded above. This evidence validates the emulator
compatibility adapter and startup path; it does not change the hardware rows,
which still require an installed-title run on a physical console.
