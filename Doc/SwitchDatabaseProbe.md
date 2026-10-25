# Switch database conformance probe

This is an isolated diagnostic, not a game release. Its title ID is
`0100F59153492000`, distinct from the game's `0100F59153491000`. It uses 16 MiB
each of SaveData and CacheStorage with 8 MiB journals, contains no AppAssets,
and does not access the game title's saved data. The test uses the real engine
SQLite amalgamation, encryption routines, and Switch adapter.

The revised 2026-09-19 **handle boundary v2** package is 12,934,360 bytes with SHA-256
`351ef9c1c4c12e0c27e35fb5aa8c4a8f881a232e1682ece60dacc4ba4ad738a1`.
Its NPDM (18 linked services, 29 linked SVCs) and NACP account/storage policy
audits pass. Physical acceptance of v2 remains pending.

The first probe (SHA-256 `4443275e8d1c4d98655504252b0781c8753d1581f877373b099d308f018ccd9e`)
failed on hardware on the first transaction: module 2, description 6457,
`WriteModeFileNotClosed`. Screenshots `2026091912180800` and `2026091912181700`
record this. Its sync hook called SaveData commit while the database stream was
still writable/open. The final errno diagnostic was stale and has been removed.
The Linux commit model now inspects actual descriptors under `/proc/self/fd`
and rejects any writable descriptor in the state archive. It reproduces that
first-transaction failure on the old implementation; the revised implementation
passes the same model. This is a regression test, not a Horizon emulator.

## Hardware test

### Current diagnostic: v7 shared native handles

Package `out/package/switch-database-probe-v7/0100f59153492000.nsp`, SHA-256
`1e6c7f263829305ff18f220596c3306d8ade7054e76a6f7b28f86753df61bafb`.

Hardware screenshots `2026091913403100` / `2026091913403900` prove v6's
second writable open fails with Horizon `0x00000e02` (TargetLocked).
Separately, game screenshot `2026091913421400` proves the ownership prefix
check appended an extra slash to the actual `pgstate:/` root.

V7 fixes the root separator check and keeps one native stream per path, with
reference-counted logical connection slots. Cipher objects and SQLite connections
remain separate. Read-first opens upgrade the native stream after closing it;
archive commits close/reopen each writable native stream once and update every
connection slot. Failed upgrades restore the reader or poison I/O safely.
The common wrapper has an optional acquisition hook; other hosts retain their
existing file opening behavior. There are no game-specific names or schemas.

The regression suite supplies a trailing-separator state root, rejects sibling
prefixes, checks RW/RW and RW/RO opens, shared commits, sequential writes,
read-first upgrade, writer-first close, and read-only enforcement. Linux also
counts actual OS descriptors to require one native handle for the two SQLite
connections. Existing copy-up, external publication, failure injection, worker
commit, and separate-launch persistence tests remain required.

Linux adapter and no-adapter probes pass; Switch packages and web/Wii U runtime
builds pass. **V7 physical acceptance is pending.** Shared native handles do not
add transaction-level SQLite locking; simultaneous transactions, sleep/resume,
power loss, quota and multiple accounts remain separate acceptance gates.

### Historical diagnostic: v6 same-file opens

Package `out/package/switch-database-probe-v6/0100f59153492000.nsp`, SHA-256
`f2cf283198b935bbf89ecdb2e92b2613e04a42ca9937cab5b760e9feec6a70ce`.
Adds second READWRITE|CREATE and READONLY opens of a database already held by
a writer. Any failure reports SQLite codes and operation-local native error.
These are required behavior checks, not expected failures marked as success.
Uses a v6 marker so previous probe success cannot skip the added tests.

Linux passes both these adapter tests and a new no-adapter native-host probe
(`playground-database-native-probe`). The latter checks encrypted same-file
reads, missing READONLY behavior, and preservation of existing bytes by
`icreateEmptyFile`. Original Android, Win32, iOS and OSX hosts use append-mode
creation. Modern RuntimePlatform used truncating `wb`; it now uses `ab` and
checks close errors. Encrypted open retries creation only for ENOENT, returning
SQLITE_CANTOPEN for other open failures. No game filenames select behavior.

Web and Wii U engine runtime rebuilds pass. Android/Windows/iOS runtime tests
have not been performed. Physical v6 results remain pending.

### Current diagnostic: v5 external commits

`out/package/switch-database-probe-v5/0100f59153492000.nsp`, SHA-256
`cfd5e15c0da024894c6f197b2bf5d9976c4a2b15c4a0e895569f4ebf61e3efb0`.
Adds actual RuntimePlatform rename/publication and cookie-clear callback tests
with two writable SQLite connections open. Tests callback failures and retry,
an explicit quiescent storage commit, rejection of a raw uncoordinated commit,
and rejection/retry when an unrelated native writer remains open. Separate
launch checks both the encrypted database and the published non-database file.

The adapter exposes `commitState` for serialized external publishers. The probe
wires its runtime callback through it; the production game host is **not yet
wired**. This does not exercise actual HOME/sleep, network-worker cancellation,
cookie HTTP traffic, or simultaneous transactions. Cookie clear is a void API;
the failure test observes its supplied commit callback rather than claiming the
caller receives a return code. Those distinctions are intentional.

Linux suite and separate-process checks pass, as do the Switch build and
NPDM/NACP/runtime audits. Physical v5 results also pass: screenshots
`2026091913192000-24D244A8BD7EC7614F74AF5C18DCE346.jpg` and
`2026091913193100-24D244A8BD7EC7614F74AF5C18DCE346.jpg`, retrieved from
`emuMMC/RAW1/Nintendo/Album/2026/09/19` over FTP, show runner PASS followed by
both `separate-launch persistence: PASS` and
`non-database publication persistence: PASS`. The FTP server returned 426 at
transfer completion, but both downloaded images rendered legibly; acceptance
is based on their visible results. A versioned probe
marker starts new tests without deleting the successful v4 data. Expected
negative tests print commit failures; the final PASS/FAIL is authoritative.

### Physical v4 result — PASS for the current probe suite

User screenshots on 2026-09-19:

- `2026091913072000-24D244A8BD7EC7614F74AF5C18DCE346.jpg`: test runner
  returns PASS, including long encryption-key filenames, injected commit
  failure, continued use of the other connection, and failed stream restoration
  without recreating the missing file.
- `2026091913073000-24D244A8BD7EC7614F74AF5C18DCE346.jpg`: a subsequent
  launch reports `separate-launch persistence: PASS` and test runner PASS.

Package SHA-256: `31f3538dd9a622ec3eb485997f7e77e0eb69a2c7116ee162b91860ad81485c02`.
These establish physical acceptance of this probe's implemented tests and
clean-exit/relaunch persistence. They do not establish two-account isolation,
abrupt power-loss/suspend behavior, concurrent transaction isolation, storage
exhaustion, or full-game integration. Firmware/account details were not supplied.

**Previous build: v4 byte-copy**, in `out/package/switch-database-probe-v4/`.
Screenshots `2026091913033300` and `2026091913034200` confirm v3 completes two
archive syncs, returns `transaction: rc=0`, and reports `reopen: PASS` on hardware.
It then terminates at fixture preparation with `filesystem error: cannot copy
file: Function not implemented`. This is direct evidence of an unsupported
copy operation, not confirmation of the earlier file-sharing hypothesis.

V4 replaces `std::filesystem::copy_file` in both fixture preparation and adapter
copy-up with a shared Switch-local checked byte-copy helper. It creates the
destination exclusively, checks read/write/close failures, and never tries to
preserve POSIX file metadata. Existing destinations are rejected without
truncation. Ciphertext verification and archive publication remain unchanged.
The local contract suite (including existing-destination rejection), separate
process persistence, Switch build, NPDM/NACP audits, and runtime isolation audit
pass. The physical results above now also pass the implemented probe suite. The SD checkpoint log
and exception handler remain enabled.

**Historical diagnostic: v3**, in `out/package/switch-database-probe-v3/`.
V2 returned to HOME after displaying `Running new database tests...`; no
matching Atmosphere crash report was supplied. Review found test fixtures were
copied by reopening a source already held by the idle writable connection.
V3 prepares those fixtures before opening the writer, and inspects ciphertext
through the unchanged lower copy instead. This fixes an invalid Horizon access
sequence, but is not yet a confirmed explanation of the hardware exit.

V3 records checkpoints in `sdmc:/switch/playground-db-probe.log` (append-only,
opened and closed for each line, outside the tested SaveData). Its C++ terminate
handler reports the active exception and waits for + instead of silently
exiting. Native faults may still terminate the process; the last persisted
checkpoint is then the evidence. The ELF is preserved beside the NSP. The host
suite, separate-process persistence, and Switch NPDM/NACP audits pass; physical
acceptance remains pending. No game data should be cleared.

Install `out/package/switch-database-probe-v5/0100f59153492000.nsp` and open
**PlaygroundOSS DB probe**. The first launch performs the tests listed below.
Exit with **+** after the result screen, then launch again. A successful second
launch prints `separate-launch persistence: PASS` by querying the first launch's
committed encrypted database. Repeat under another selected account to exercise
account separation. A failure screen should be captured including the operation
and native result; there is no Lua assertion or base64 error payload here.

The `.nro` is only an SD-filesystem diagnostic. It cannot validate installed
title SaveData semantics. Existing yuzu 1734 is explicitly not an authoritative
emulator for Horizon storage (see the previous hardware acceptance record).

The probe retains its own small test files, including failed-copy-up evidence.
It performs no automatic reset of game or probe saves. After its first successful
run it checks persistence on subsequent launches. A fresh account supplies a
fresh first-run test.

## Current test coverage

- Create an encrypted database, insert/update in a transaction, close/reopen.
- Verify database bytes are ciphertext, not a plain SQLite header.
- Assert successful write transactions call the sync/commit boundary.
- Reject a missing named database without creating one.
- Copy an encrypted lower database to account storage and mutate the copy.
- Read the lower database through the asset namespace and verify its row is unchanged.
- Inject a commit failure before copy-up publication; ensure no final upper
  database appears, then retry successfully.
- Create/reopen a database with a logical filename over 64 bytes, preserving
  its encryption key through the filesystem codec.
- Reject traversal and reserved staging paths.
- Inject transaction commit failure and require `SQLITE_IOERR_FSYNC`.
- Keep a second writable SQLite connection open through copy-up and repeated
  commits; verify it remains usable after an injected commit failure.
- Inject a missing file during stream restoration; require an I/O error,
  safe connection cleanup, and no replacement empty file.
- Verify the committed database in a separate process/launch.

Linux executes exactly the same adapter/probe source with a test commit callback;
it cannot prove Horizon service behavior. Its current suite and separate-process
check pass. Switch ELF/NRO build and runtime source-isolation audit pass. The Web
engine runtime builds. Wii U rebuild is blocked by the unavailable Docker daemon;
Windows and Android regression builds have not been rerun for this checkpoint.

Short-write/seek/flush fault injection, storage exhaustion, interruption at each
publication boundary, locking/concurrent transactions, real-hardware suspend,
two-account evidence, and the remaining platform regression checks are still
required. Do not call this a completed database implementation or wire it into
the game on the strength of the current host results alone.

The adapter suspends **all registered writable database streams**, commits,
then reopens without CREATE and restores positions. SQLite connection and
cipher objects stay alive. Copy-up uses the same boundary. Any close/restore
failure poisons further commits until restart. A writable non-database handle
in the same archive still causes commit failure; this is propagated, not hidden.
The current contract requires serialized engine-thread I/O. Multiple open
connections are tested, but concurrent transactions and archive-wide atomicity
are not established; `journal_mode=OFF` is not a rollback implementation.

## Rebuild

```sh
cmake --build --preset linux-release --target playground-database-contract-probe
out/build/linux-release/Engine/modern/runtime/playground-database-contract-probe /tmp/new-db-probe
out/build/linux-release/Engine/modern/runtime/playground-database-contract-probe --verify /tmp/new-db-probe

cmake --build --preset switch-release --target playground-switch-database-probe-stage playground-switch-database-probe-nro
```

The Linux scratch directory must not exist for the first command. The Switch
stage is under `out/build/switch-release/Engine/modern/host/switch/database-probe/package-stage`.
Use the existing `scripts/package_switch_nsp.py` with that stage and a local
user-owned keyset. The packer rejects the game title ID for a probe package.
The packaging tool used for this checkpoint is hacBrewPack 3.05, source commit
`745b16ecfc9ce055743067d200572204cb2aac6c` from its
[source mirror](https://github.com/gayhearts/hacBrewPack/tree/745b16ecfc9ce055743067d200572204cb2aac6c).
