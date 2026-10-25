# CacheStorage download writer coordination — 2026-09-19

Game package: `out/package/switch-cache-writer-coordination/0100f59153491000.nsp`.
SHA-256: `ae70e68473c549b610a2a8c889e81745c6e75e4d6d158db2de43051f2feee470`.
The matching ELF is retained beside the package. Physical acceptance is pending.

Hardware game screenshot `2026091914023600` shows the generic download failure
popup at DL 5/21, UNZ 2/21. It contains no native error code, so it does not by
itself establish the underlying failure. No runtime log was available in the
checked SD locations.

The source exposes a reproducible failure: `DesktopTmpFile::closeTmp()` commits
CacheStorage as soon as one file closes, while other downloads/extraction files
can remain writable. Horizon requires archive writers to be closed at commit.
The HTTP completion path maps a close failure to error 23; the unzip path maps
it to extraction error -5. Retrying can therefore advance some files and fail
again, without any server or transfer failure.

`playground-archive-writer-probe` runs the actual RuntimePlatform temporary-file
and output-stream paths against a Linux model that examines `/proc/self/fd` and
rejects archive commits with writable descriptors. Without coordination it
reproduces the close failure. With the Switch coordinator the same scenario
passes. This is a regression model, not a substitute for hardware verification.

## Implementation

`SwitchArchiveWriters` excludes native open/write/close during archive commit.
Ordinary operations on independent files may run concurrently (see the
2026-09-19 stall follow-up below).
At a commit it flushes and fsyncs active writers, records positions, closes
native handles, commits the archive, and reopens with `r+b` at the saved offsets.
Logical streams and encryption state remain alive. It never recreates a missing
file on restore, never truncates a duplicate open, and propagates commit errors.
Failed stream recovery poisons subsequent I/O instead of silently losing bytes.

RuntimePlatform provides an optional content-writer factory. Only the installed
Switch host selects this implementation. Default writers on other platforms
continue to use ordinary stdio. The database adapter is unchanged. The host's
CacheStorage publication and lifecycle commits use the same coordinator.

Archive commits may persist partial in-progress temporary files as well as the
completed file, as archive-wide commits inherently do. This does not introduce
transactional extraction or change asset validation/recovery policy. The
coordinator does not hide untracked native writers; they still cause an error.

## Verification

Build `playground-archive-writer-probe` using the `linux-release` preset and run
it with a new scratch directory. Coverage includes:

- uncoordinated rejection, then overlapping downloads/extraction/publication;
- exact payloads and positions after reopening;
- 1,000 worker writes overlapping 64 archive commits;
- duplicate/aliased-open protection and archive-root boundary checks;
- injected commit failure and retry, including untracked native writers;
- restore failure without recreation or false success.

Database adapter, no-adapter and path-policy regressions pass. Web and Wii U
runtime builds pass. Switch runtime/packaging audits check coordinator wiring.
**Physical download acceptance remains pending.** No game data is cleared and
no game-specific filenames or schemas are introduced.

## Download speed investigation

Speed-test package: `out/package/switch-download-speed-test/0100f59153491000.nsp`.
SHA-256: `674e4bc4a91a2122387c68251f3af397cd8596c1328e43b326bbe5bb87f1faea`.
Matching ELF is retained; the soft-restart correction is included.

The user reports that the coordinated build completes initial downloading, with
one remaining popup, at roughly 200 on the game's speed display. That display
calculates bytes divided by 1024 and elapsed seconds (KiB/s despite the label).
The subsequent soft-restart defect is recorded separately in
`SwitchNetworkRestart.md`.

Switch writers now supply a 64 KiB stdio buffer rather than relying on the
installed newlib header's 1 KiB default. This reduces potential filesystem-call
overhead inside synchronous curl write callbacks without moving or omitting
commit boundaries. Buffer ownership spans native close/reopen; every reopened
stream is configured before I/O. Multi-buffer payload tests verify exact bytes
and positions across repeated commits. A host syscall trace confirms coalesced
writes, but this is **not a measured Switch speedup**.

Temporary diagnostic snapshots append to
`sdmc:/switch/playground-download-profile.log` on the first content commit,
every 32nd commit, and failures. They contain cumulative writer bytes/calls,
write time, write-lock wait, close time, commit time/failures, active writers,
and completed-network-transfer counts/bytes/time. No request URLs, headers or
payloads are logged. Timings may overlap across threads; transfer time includes
write callbacks and must not be interpreted as independent wire-only latency.
This instrumentation should be removed after the hardware bottleneck is known.

The SD log did **not** materialize on the user's installed-title run. The
installed host does not mount `sdmc:` and its narrow filesystem capability
policy deliberately does not grant general SD access. The attempted `fopen`
was unchecked and silently failed; no timing conclusion can be drawn from
that run. The failed file logger has now been removed. Temporary **Minus**
button diagnostics instead show a snapshot in the existing system message
applet, requiring no new filesystem/service permissions. Take a screenshot
of the details after a short download run. Capture occurs before the applet
opens; repeated snapshots can include time spent in earlier applets, so use
the first snapshot from a fresh launch for timing analysis.

On-screen diagnostic package:
`out/package/switch-download-profile-screen/0100f59153491000.nsp`, SHA-256
`e00c9305a9f1dd4ce5f1589cd9b8461787f74460564bbcaeafc70845537aefd2`.
Matching ELF is retained alongside it. Build/audits and host archive-writer
regressions pass; the system diagnostic display still needs hardware checking.

## Measured bottleneck and package-level batching

Hardware screenshot `2026091914531800` shows 2,597 commits consuming 219.448 s,
317.486 s of summed writer-lock waits, 13.375 s writing 121,276,619 bytes
(downloads plus extraction), and 22.663 s closing streams. There were zero
commit failures. Completed transfers totalled 10,146,348 bytes over 422.307
summed transfer-seconds. These overlapping timings are not wall-clock totals,
but establish substantial archive-commit contention.

The ZIP worker now brackets extraction with an optional, platform-neutral
file-write batch hook. Its defaults are no-ops; only the Switch content store
enables deferral. Each file still closes and reports write/close failures.
Per-file commit requests on that extraction thread defer to a checked commit
before the worker reports successful completion. Corruption or extraction
failure aborts the batch; a failed final commit becomes extraction error -5.
Scope cleanup balances the boundary if extraction unwinds.

Deferral is per thread. Other download completions continue to commit, and
explicit HOME/sleep commits always force the archive boundary. This is **not
atomic ZIP installation or rollback**: partial extraction may still be persisted
by another archive commit, as before. Package completion is not acknowledged
until the final commit succeeds. No filename, game schema, arbitrary delay or
unchecked commit-success shortcut is involved. Power-loss recovery still needs
physical acceptance testing.

`scripts/test_switch_archive_batch.py` generates a 256-member ZIP and a bad-CRC
variant and runs the real engine extraction worker in the host probe. The same
extraction makes 256 native commits without the hook and one with it. Payload
bytes, a concurrent open download, final-commit failure/retry, corrupt-ZIP abort,
thread-local deferral, forced lifecycle commits, nesting rejection, and unclosed
output rejection are covered. The temporary Minus diagnostic remains available
to measure the result on hardware.

## Stall follow-up: independent IO and audio deadlines (2026-09-19)

Hardware screenshot `2026091915265600` is **not download completion**: the user
reports being only halfway through, with extreme slowdown and audible stutter.
It shows 36 completed transfers / 356,108,764 bytes, zero active network leases
or tracked writers at that instant, 50 commits / zero failures / 7.714 seconds,
140.987 seconds writing, 138.115 seconds closing, and 330.651 seconds waiting
for the writer lock. These are cumulative, overlapping thread timings. They
do not identify the stalled phase or establish wire speed. Zero active network
leases can mean processing/waiting, not completion of the overall update.

Two source-level problems are addressed without modifying the common engine:

* The coordinator held one mutex across every fopen/fwrite/fclose. A blocked
  file therefore blocked every other download/extraction writer and the
  main-thread diagnostic snapshot. Ordinary operations now take a shared
  archive gate and a per-file mutex. Registry/error/statistics locking is
  short and never covers filesystem IO. An admission turnstile prevents new
  writers continuously overtaking a waiting exclusive commit. The commit
  still flushes/closes **all** native writers, commits, and restores positions;
  no durability checks were removed. Reservations protect duplicate opens,
  including failed-open cleanup, before any truncating fopen.
* Both Switch audio threads used default std::thread scheduling alongside
  background download/extraction workers. libnx newlib starts those at
  priority 59. The audio output thread now selects 40 and decoder 41, ahead
  of the application's main-thread priority 44. Output still blocks on audout
  and decoding still sleeps between passes; no busy-polling was introduced.
  The package explicitly permits svcSetThreadPriority (0x0d); failures are
  reported rather than assumed away. This is a deadline-protection measure,
  not proof that CPU starvation alone caused the physical stall.

The Minus snapshot adds active ZIP batches, longest individual write/close/
commit durations, longest audio decode/fill durations, and priority failures.
Audio maxima include scheduling and IO delays, not pure CPU execution time.

Regression coverage adds blocked FIFO writes and closes with independent
file IO and snapshot progress, four parallel writers/closers under repeated
forced commits with exact-byte verification, and failed-open reservation
cleanup. Existing real-ZIP, corrupt-ZIP, commit failure/retry, restore failure,
and lifecycle tests pass. The Switch target and source/linked runtime audit
pass; the audit also checks audio priorities and the NPDM syscall declaration.
No other platform handler, game schema, URL, or saved-data content was changed
by this follow-up. Hardware performance and automatic restart acceptance are
still pending.

SDK evidence:
[libnx thread defaults](https://github.com/switchbrew/libnx/blob/master/nx/source/runtime/newlib.c),
[libnx filesystem operations](https://github.com/switchbrew/libnx/blob/master/nx/source/runtime/devices/fs_dev.c).

Test package: `out/package/switch-download-io-concurrency/0100f59153491000.nsp`
(matching ELF alongside it), SHA-256
`657842495a31b8c0c37c5a7bf1a72e5a8d683167d7d02dbdeed902ee7f2f6dc6`.

## Regression rollback and bounded file diagnostics

The user reports the independent-IO/raised-audio-priority build made audio
stutter worse and prevented opening the menu. Its hardware acceptance FAILED.
Neither a memory leak nor the exact scheduling/storage cause is established.
The package-level batching remains, but native open/write/close/commit are
serialized again and both audio threads use their original default priority.
Snapshots retain a separate short-held metadata lock, so sampling does not
wait on the filesystem operation. An atomic IO phase, operation age and waiter
count identify long-running open/write/close/commit operations.

`PLAYGROUND_SWITCH_FILE_DIAGNOSTICS` is OFF by default. The opt-in diagnostic
build adds only SD access (bit 21, resulting FS mask `0x200021`) to the existing
mask, plus resource-count query SVC 0x31. It does not enable priority-changing
SVC 0x0d. The NPDM audit requires explicit `--allow-diagnostic-sd` for this
variant; ordinary builds still require exactly the original mask.

The independent sampler writes every five seconds to
`sdmc:/switch/PlaygroundOSS/logs/runtime-current.log`, rotating to
`runtime-previous.log` at launch or 512 KiB. Maximum generated log data is
1 MiB, with no queued backlog. Each record is flushed, synced and closed;
there is no SaveData/CacheStorage log writer or extra archive commit. Prior
records should survive a forced application exit, but a system crash/storage
failure can still lose the last record. SD IO itself may stall the sampler;
it is not an out-of-process watchdog. The next record reports how long the
previous sample (including log IO) took to expose diagnostic overhead.

Records contain uptime, completed frame count, main-loop phase/age, process
mapped memory, allocator used/free bytes and free-block count, resource-limit
thread/session counts and query results, storage timings/active writers/ZIP
batches, and audio maxima. Sessions are not a count of all kernel handles.
Network metrics use a nonblocking snapshot and explicitly report contention.
No URLs, credentials, request bodies or game data are logged. Sampler shutdown
joins before platform/coordinator destruction. Diagnostic status appears in
the Minus dialog (2 active, -1 failed, 0 disabled).

Tests cover log rotation, restart retention, oversize rejection and log-path
failure, diagnostic responsiveness during a blocked write/close, and the
existing exact-payload/commit/ZIP/lifecycle regressions. Both the default
build and enabled diagnostic build compile; enabled NPDM audit passes with
31 linked SVCs and FS mask `0x200021`. Hardware validation remains pending.
Do not assume the log exists until it has been retrieved or status verified.

SD permission reference: [Atmosphere access-control definitions](https://github.com/Atmosphere-NX/Atmosphere/blob/master/libraries/libstratosphere/include/stratosphere/fssrv/impl/fssrv_access_control_bits.hpp).
