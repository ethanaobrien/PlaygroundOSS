# Switch database architecture proposal

## Exploratory game integration — 2026-09-19

After physical v4/v5 probe passes, the Switch game host now registers the
adapter before runtime/game initialization and destroys it only after closing
Lua/core/language databases. Runtime SaveData callbacks and suspend commits
route through the adapter; its underlying callback alone commits raw storage.
Before registration, bootstrap commits still use storage directly.

Network cleanup may invoke commits on worker threads. Optional generic
`lockIO`/`unlockIO` hooks now serialize encrypted open/close/read/write/size/sync
and path resolution against archive close/commit/reopen. The Switch adapter
implements a recursive mutex; other platforms retain no-op hooks. A local test
commits from a worker while the engine reads two writable connections. It
passes, but does not establish concurrent transaction isolation. This extends
the earlier single-engine-thread contract only to native I/O serialization.

Game boot-test package: `out/package/switch-game-boot-20260919/0100f59153491000.nsp`.
Physical game acceptance remains pending; the historical proposal/checkpoints
below describe the earlier stages. No console work has been pushed.

## Implementation checkpoint — 2026-09-19

### Hardware correction: writable handle lifetime

The first isolated probe failed with Horizon `WriteModeFileNotClosed` (2-6457).
Flushing an existing descriptor was not sufficient. The candidate now adds
generic `opened`/`closing` stream-lifetime callbacks to the database boundary.
Only the Switch adapter tracks writable stream slots, suspends all of them for
an archive commit, and restores them using non-creating opens at their saved
positions. SQLite connections and cipher state remain intact. Copy-up shares
this commit boundary. Failure propagates; failed close/restoration prevents
later commits until restart. Default callbacks do nothing on other platforms.

The Linux probe now rejects commits with actual writable descriptors in the
archive. The old implementation fails this regression; the new one passes,
including multiple open connections, failed commit recovery, failed stream
restoration, and separate-process persistence. Physical v2 results are pending.
This is still single-engine-thread operation, not a claim of concurrent
transaction isolation or coordination with non-database archive writers.

A preliminary implementation now exists and is linked into the standalone
database probe, **not the game host**. Physical acceptance remains pending.
The sections below this checkpoint preserve the original proposal; the
following measured refinements supersede its illustrative interface:

- `DatabasePlatform.h` defines a host-registered resolution/sync/key-path
  interface. Its default uses the established `getFullPath` behavior. All three
  production open sites use it. The Lua frontend explicitly tells resolution
  about its existing writable-path flag promotion before storage is selected.
- `SwitchDatabase` owns the device-path VFS clone, namespace routing, verified
  ciphertext copy-up, and SaveData commit callback. It is currently exercised
  only by the probe. It does not inspect game database names or directory depth.
- The existing cipher remains in use. Logical key paths survive physical path
  encoding. Shared VFS fixes check seek/write/flush errors, bound creation retry,
  return errors for unsupported truncation/WAL, initialize reserved-lock output,
  and return `SQLITE_NOTFOUND` for unhandled file controls. The latter matters
  because `SQLITE_OK` makes SQLite treat a PRAGMA as already handled.
- The legacy wrapper's `xSync` was a no-op. It now flushes the stream and invokes
  the registered adapter. The adapter calls `fsync` on existing descriptors,
  closes all registered writable streams, commits, and restores those streams.
  It never reopens a writable file merely to query its size. The actual bundled SQLite was tested
  to call this hook with `journal_mode=OFF` and `synchronous=FULL`.
- Copy-up verifies every byte rather than calculating an additional SHA-256.
  A fixed reserved sibling stage is valid only under the current engine-thread
  open model. Concurrent opens, transactional isolation, quota exhaustion,
  interruption during publication, and physical mount commit semantics still
  require conformance evidence before game integration.
- Unnamed temporary databases fail explicitly in this candidate; the original
  wrapper does not implement them. No missing named database becomes temporary.

See [SwitchDatabaseProbe.md](SwitchDatabaseProbe.md) for the exact tests,
package, remaining work, and hardware instructions. Passing the probe's current
subset is not the entire completion gate below. Wii U adapter migration is
still pending, and neither console platform is approved for pushing.

## Original proposal status

The following design was proposed before implementation. Switch database support is blocked until this
design has a standalone physical-hardware acceptance result. The game is not
the first test fixture.

The cleanup preceding this proposal uses
`f56fe3a4977988053b76765b1bfe5c2617b0873c` as the pre-Switch reference. It
removes the rejected Switch SQLite sources, host registration, shared
`IPlatformRequest` database API, RuntimePlatform database copy-up/commit
policy, common-engine Switch branches, and database-specific probes that
claimed unproven behavior. Android's established writable-path flag promotion
is restored.

## Evidence and constraints

The design must preserve the following independently observed facts:

- Android resolves a logical URI with `IPlatformRequest::getFullPath()`. A
  writable result causes `CKLBLuaDB` to promote the caller's flags to
  `SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE`. The engine uses its encrypted
  SQLite wrapper and `journal_mode=OFF`.
- The old draft at `bc91fb24f12f8626ff2d27ac2460f65fbc4c56a9` proved that
  libnx can run the engine's database traffic when a small `unix-none` clone
  preserves device-prefixed paths and the existing encrypted wrapper performs
  the actual I/O. It is evidence, not source to copy: its slash-depth storage
  classifier, periodic global commits, SD fallback, and common runtime
  conditionals are not acceptable architecture.
- Physical hardware disproved the later raw-VFS, synthetic-metadata,
  `:memory:` fallback, hard-coded database, shared copy-up, and close-time
  patch sequence. Emulator success is insufficient.
- The engine has three production SQLite open sites: `CKLBDatabase`,
  `CKLBLuaDB`, and `CKLBLanguageDatabase`. A solution that covers only the
  currently failing call site is incomplete.
- SaveData is selected-account private and transactionally committed.
  CacheStorage is replaceable and title-shared. Database ownership therefore
  cannot be guessed from a filename, extension, directory depth, Lua module,
  or this particular game.
- Logical paths must remain unchanged. The existing Switch component codec is
  applied exactly once at the native storage boundary.

## Chosen boundary

Add one database-scoped adapter seam. Do not add database methods to
`IPlatformRequest`, Switch logic to `RuntimePlatform`, or platform `#if`
branches to any database frontend.

The common interface should model only what SQLite opening needs:

```cpp
struct DatabaseOpenRequest {
    const char *logicalUri;
    int sqliteFlags;
};

struct DatabaseOpenTarget {
    std::string nativePath;
    const char *vfsName;       // null selects the established default
    bool readOnly;
};

class IDatabasePlatformAdapter {
public:
    virtual bool initialize(std::string &error) = 0;
    virtual bool resolve(const DatabaseOpenRequest &request,
                         DatabaseOpenTarget &target,
                         std::string &error) = 0;
    virtual bool sync(const char *nativePath, std::string &error) = 0;
    virtual void shutdown() = 0;
    virtual ~IDatabasePlatformAdapter() = default;
};
```

A small database broker owns the active adapter. With no adapter installed it
reproduces the pre-Switch behavior by using `getFullPath()`, the default VFS,
and a no-op archive sync. Android and existing desktop builds therefore do not
need platform handlers.

The three production SQLite open sites call the broker. That is the complete
one-time common-code surface. The broker initializes encryption before every
kind of database open, so behavior no longer depends on which singleton opens
first. Existing frontend-specific flag behavior and SQL pragmas remain where
they are; an adapter may not rewrite them.

The encrypted VFS gains one generic sync call after a successful stream flush
and before reporting `xSync` success. Its default is a no-op. This is the only
durability hook in the common VFS; it contains no platform names, mount names,
or storage classification. A dirty file is never committed after a failed
write or flush.

Only one adapter may be installed in a process. The host installs it before
`GameSetup()` and removes it only after every database has closed. Registration
failure stops startup.

## Source ownership

The intended layout is:

```text
Engine/source/Database/DatabasePlatformAdapter.{h,cpp}  stable broker/default
Engine/source/Database/CKLBDatabase.cpp                 one broker call
Engine/source/Database/CKLBLuaDB.cpp                    one broker call
Engine/source/Database/CKLBLanguageDatabase.cpp         one broker call

Engine/modern/runtime/switch/include/Playground/Switch/SwitchDatabase.h
Engine/modern/runtime/switch/src/SwitchDatabase.cpp
Engine/modern/host/switch/tools/switch_engine_main.cpp  install/uninstall only

Engine/modern/runtime/wiiu/include/Playground/WiiU/WiiUDatabase.h
Engine/modern/runtime/wiiu/src/WiiUDatabase.cpp
```

Switch and Wii U do not share an implementation. They implement the same
interface in their own platform directories. Adding another platform adds a
new implementation and one host registration; it does not edit a common
switch statement or add another preprocessor branch. The current Wii U
database conditionals are checkpoint debt and should migrate to its adapter
after the Switch contract is proven.

## Switch adapter behavior

`SwitchDatabase` is constructed with the mounted `SwitchStorage` roots and the
existing physical path mapper. It contains no application or game identity.

Initialization registers a named clone of SQLite's `unix-none` VFS whose only
changed operation is `xFullPathname`: valid libnx device paths such as
`pgstate:/...` and `pgcache:/...` are already absolute and are returned
unchanged. The clone performs no database I/O. The engine's established
encrypted wrapper remains the I/O layer, matching Android and the working
draft evidence.

Resolution is based only on the logical namespace and SQLite access flags:

| Request | Result |
| --- | --- |
| unnamed SQLite temporary database | Preserve SQLite's native temporary-database behavior; never use it as fallback for a named URI. |
| `file://state/...` | Selected-account SaveData. |
| read-only `file://external/...` | Existing SaveData upper object, otherwise existing CacheStorage lower object. Missing named objects fail. |
| writable `file://external/...` | Selected-account SaveData. If only a lower object exists, publish a byte-identical copy to SaveData and commit it before opening. |
| `asset://...`, `file://asset/...`, `file://install/...` | Existing downloaded/install object using the normal asset precedence and read-only result. |
| invalid, traversing, or unresolved named URI | Fail with the logical URI, SQLite flags, storage class, and native result in the error; never substitute an empty database. |

Copy-up is an adapter-owned storage transaction: copy to a unique
same-directory temporary object, verify byte count and SHA-256, close it,
commit SaveData, atomically rename it, commit the publication, then open it.
Failure removes or leaves only an authenticated recovery temporary; it never
publishes a partial upper object or modifies the lower object. The path codec
is applied after logical resolution and only once.

`sync()` identifies ownership from the already resolved native root, not from
the database name. SaveData files flush and commit SaveData; CacheStorage files
flush and commit CacheStorage; install files are read-only. Lifecycle commits
remain a second safety boundary. There is no periodic ten-second database
workaround.

SQLite locking capabilities must be conservative. The title is a single
process and the legacy engine selects `journal_mode=OFF`, so the adapter may
use `unix-none`, but it must not advertise atomic-sector or power-safe overwrite
properties that Horizon has not proven. Unsupported WAL shared-memory calls
return the appropriate SQLite error immediately rather than spinning.

## Conformance before game testing

Create a game-independent `playground-switch-database-probe` and a host model
using the same adapter contract. The probe bundles a tiny database encrypted by
the existing engine algorithm and performs these checks:

1. Open and query a read-only packaged encrypted database.
2. Create, insert, update, delete, close, reopen, and verify a writable
   SaveData database.
3. Copy up an encrypted lower database byte-for-byte, mutate the upper copy,
   and prove the lower bytes did not change.
4. Reject a missing named database without creating a zero-byte file or
   opening `:memory:`.
5. Exercise short read/write, failed seek, file-size, flush, full-storage, and
   commit error paths with exact SQLite result reporting.
6. Exercise a logical component longer than 64 bytes and prove list, reopen,
   rename, and removal through the path codec.
7. Relaunch after injected interruption before copy, before first commit,
   before rename, and before publication commit.
8. Run under two selected accounts and prove distinct writable state with a
   shared unchanged lower seed.
9. Suspend and terminate immediately after a committed transaction, then
   reopen and verify it on the next launch.
10. Audit the final ELF and source: no game database strings, no
    `SwitchDatabase` reference outside the Switch runtime/host, no database
    API in `IPlatformRequest` or `RuntimePlatform`, and no `__SWITCH__` in
    `Engine/source/Database`.

The host model must pass first, followed by the standalone probe in a current
emulator, followed by the same probe on physical hardware. Only then is the
full game a valid integration test. A failure returns to the adapter or broker;
it must not be patched by database filename or Lua call site.

## Explicitly rejected approaches

- Restoring the old draft wholesale.
- Expanding `IPlatformRequest` or `RuntimePlatform` with database routing.
- Adding platform conditionals to `CKLBDatabase`, `CKLBLuaDB`, or
  `CKLBLanguageDatabase`.
- Selecting storage or VFS by filename, extension, directory depth, caller,
  SQL contents, or game identity.
- Sending writable databases through a raw Unix VFS that bypasses the engine's
  encryption format.
- Replacing an unresolved named database with `:memory:`.
- Rewriting caller SQLite flags in a platform adapter.
- Maintaining synthetic sizes or special cases for one known database.
- Treating emulator boot, compilation, or extraction success as database
  acceptance.

## Completion gate

Implementation is complete only when the source/ELF isolation audit, host
model, emulator probe, physical-hardware probe, two-account test, interruption
matrix, and Android/Linux/Windows/Web regression builds all pass. Wii U then
migrates to its own adapter and repeats the same platform-independent probe on
physical hardware before either console platform is pushed.
