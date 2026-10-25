#include "Playground/Runtime/RuntimePlatform.h"
#include "CKLBDatabase.h"
#include "CPFInterface.h"
#include "encryptFile.h"
#include "Playground/Switch/SwitchDatabase.h"
#include "Playground/Switch/SwitchPath.h"
#include "Playground/Switch/SwitchFileCopy.h"

#include <cstdio>
#include <cerrno>
#include <filesystem>
#include <string>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <vector>
#include <thread>
#include <atomic>

// Exercise the same SQLite amalgamation and encrypted wrapper as the engine,
// without any Lua or game data. Never use a player's database as a test fixture.
namespace {
unsigned syncCalls = 0;
int (*originalSync)(sqlite3_file *, int);
int recordSync(sqlite3_file *file, int flags) {
  ++syncCalls;
  return originalSync(file, flags);
}
}
extern sqlite3_io_methods gSQLiteEncryptIO;
// Host-owned progress reporting; Switch also persists this outside SaveData.
extern void databaseProbeCheckpoint(const char*);
extern std::string databaseProbeNativeError();
extern bool databaseProbeSingleNativeHandle(const char*);

int runDatabaseContractProbe(const std::filesystem::path& install,
    const std::filesystem::path& content, const std::filesystem::path& state,
    const playground::switch_runtime::SwitchDatabase::Commit& commit, bool verifyOnly) {
  if (!verifyOnly) {
    for (const auto& directory : {install, content, state}) {
      databaseProbeCheckpoint(directory.string().c_str());
      std::error_code error;
      std::filesystem::create_directories(directory, error);
      if (error) {
        databaseProbeCheckpoint(("Directory setup failed: " + error.message()).c_str());
        return 1;
      }
    }
  }
  databaseProbeCheckpoint("Construct runtime platform");
  playground::switch_runtime::SwitchDatabase* coordinator = nullptr;
  unsigned externalCommits = 0;
  bool externalCommitOk = false;
  playground::runtime::RuntimePlatform platform(install.string(), content.string(),
      state.string(), nullptr, [&] {
        ++externalCommits;
        std::string error;
        externalCommitOk = coordinator ? coordinator->commitState(error) : commit(error);
        return externalCommitOk;
      }, {});
  CPFInterface::getInstance().setPlatformRequest(&platform);
  struct DetachPlatform {
    ~DetachPlatform() { CPFInterface::getInstance().setPlatformRequest(nullptr); }
  } detachPlatform;
  unsigned commits = 0;
  bool failCommit = false;
  std::string interruptRestore;
  databaseProbeCheckpoint("Construct database adapter");
  playground::switch_runtime::SwitchDatabase adapter(install, content,
      state / "", true, [&](std::string& error) {
        ++commits;
        if (failCommit) error = "injected commit failure";
        if (failCommit || !commit(error)) return false;
        if (!interruptRestore.empty()) {
          std::error_code ec;
          std::filesystem::rename(interruptRestore, state / "restore-held.db", ec);
          if (ec) { error = ec.message(); return false; }
          interruptRestore.clear();
        }
        return true;
      });
  adapter.setTrace(databaseProbeCheckpoint);
  adapter.setNativeError(databaseProbeNativeError);
  databaseProbeCheckpoint("Register database adapter");
  if (!adapter.initialize()) { databaseProbeCheckpoint(adapter.error()); return 1; }
  coordinator = &adapter;
  struct ResetCoordinator {
    playground::switch_runtime::SwitchDatabase*& pointer;
    ~ResetCoordinator() { pointer = nullptr; }
  } resetCoordinator{coordinator};
  databaseProbeCheckpoint("Initialize cipher");
  initNMAsset(0);
  databaseProbeCheckpoint("Initialize encrypted VFS");
  if (!initializeEncryptedSQLiteVFS())
    return 1;
  originalSync = gSQLiteEncryptIO.xSync;
  gSQLiteEncryptIO.xSync = recordSync;
  sqlite3 *db = nullptr;
  const int createFlags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE;
  bool readOnly = false;
  databaseProbeCheckpoint("Resolve first database");
  const char* resolved = resolveDatabaseOpenPath("file://external/contract.db",
      verifyOnly ? SQLITE_OPEN_READONLY : createFlags, false, &readOnly);
  if (!resolved) { std::fprintf(stderr, "%s\n", adapter.error()); return 1; }
  const std::string path(resolved);
  delete[] resolved;
  if (verifyOnly) {
    int rc = sqlite3_open_v2(path.c_str(), &db, SQLITE_OPEN_READONLY, nullptr);
    sqlite3_stmt* statement = nullptr;
    if (!rc) rc = sqlite3_prepare_v2(db, "SELECT value FROM sample WHERE id=1", -1,
                                    &statement, nullptr);
    const bool valid = !rc && sqlite3_step(statement) == SQLITE_ROW &&
        std::string(reinterpret_cast<const char*>(sqlite3_column_text(statement, 0))) == "persisted";
    sqlite3_finalize(statement);
    sqlite3_close(db);
    std::printf("separate-launch persistence: %s\n", valid ? "PASS" : "FAIL");
    std::ifstream payload(state / "published-probe-payload");
    std::string text;
    std::getline(payload, text);
    const bool published = text == "download bytes";
    std::printf("non-database publication persistence: %s\n", published ? "PASS" : "FAIL");
    return valid && published ? 0 : 1;
  }
  databaseProbeCheckpoint("Open first writable database");
  int rc = sqlite3_open_v2(path.c_str(), &db,
                          SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr);
  databaseProbeCheckpoint("Execute first transaction");
  if (rc == SQLITE_OK)
    rc = sqlite3_exec(db,
        "PRAGMA journal_mode=OFF; PRAGMA synchronous=FULL;"
        "CREATE TABLE sample(id INTEGER PRIMARY KEY, value TEXT);"
        "BEGIN; INSERT INTO sample VALUES(1,'first');"
        "UPDATE sample SET value='persisted' WHERE id=1; COMMIT;",
        nullptr, nullptr, nullptr);
  std::printf("transaction: rc=%d sync-calls=%u error=%s\n", rc,
              syncCalls, db ? sqlite3_errmsg(db) : "no handle");
  databaseProbeCheckpoint("Close first connection");
  const int closed = sqlite3_close(db);
  if (rc != SQLITE_OK || closed != SQLITE_OK || syncCalls == 0 || commits == 0)
    return 1;
  db = nullptr;
  rc = sqlite3_open_v2(path.c_str(), &db, SQLITE_OPEN_READONLY, nullptr);
  sqlite3_stmt *statement = nullptr;
  if (rc == SQLITE_OK)
    rc = sqlite3_prepare_v2(db, "SELECT value FROM sample WHERE id=1", -1,
                            &statement, nullptr);
  const bool matched = rc == SQLITE_OK && sqlite3_step(statement) == SQLITE_ROW &&
      std::string(reinterpret_cast<const char *>(sqlite3_column_text(statement, 0))) ==
          "persisted";
  sqlite3_finalize(statement);
  sqlite3_close(db);
  std::printf("reopen: %s\n", matched ? "PASS" : "FAIL");
  if (!matched) return 1;

  // Prepare lower fixtures while the source has no open writable handle.
  // Horizon forbids copy_file from reopening a file held by a writer, unlike
  // Linux. Fixture setup is not a test of concurrent native access.
  databaseProbeCheckpoint("Prepare closed-file fixtures");
  std::filesystem::create_directories(content / "seed");
  std::string copyError;
  if (!playground::switch_runtime::copyFileBytes(path,
      (content / "seed/contract.db").string(), copyError)) {
    databaseProbeCheckpoint(copyError.c_str()); return 1;
  }
  std::filesystem::create_directories(content / "failure");
  if (!playground::switch_runtime::copyFileBytes(path,
      (content / "failure/contract.db").string(), copyError)) {
    databaseProbeCheckpoint(copyError.c_str()); return 1;
  }
  if (playground::switch_runtime::copyFileBytes(path,
      (content / "seed/contract.db").string(), copyError)) return 1;
  std::puts("byte copy refuses existing destination: PASS");

  // Keep a separate writable connection alive across every later commit and
  // copy-up. Closing only the connection being synced must fail the host model.
  struct Connection {
    sqlite3* handle = nullptr;
    ~Connection() { if (handle) sqlite3_close(handle); }
  } idle;
  rc = sqlite3_open_v2(path.c_str(), &idle.handle, SQLITE_OPEN_READWRITE, nullptr);
  if (!rc) rc = sqlite3_exec(idle.handle, "PRAGMA journal_mode=OFF;",
                             nullptr, nullptr, nullptr);
  if (rc) return 1;
  for (const int mode : {SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, SQLITE_OPEN_READONLY}) {
    databaseProbeCheckpoint(mode & SQLITE_OPEN_READWRITE
        ? "Same-file second writable connection" : "Same-file reader beside writer");
    sqlite3* alias = nullptr;
    const int opened = sqlite3_open_v2(path.c_str(), &alias, mode, nullptr);
    if (opened) {
      const std::string detail = "Same-file open failed: flags=" + std::to_string(mode) +
          " SQLite=" + std::to_string(opened) + " extended=" +
          std::to_string(sqlite3_extended_errcode(alias)) + ": " + sqlite3_errmsg(alias) +
          "; " + adapter.error();
      databaseProbeCheckpoint(detail.c_str());
      sqlite3_close(alias);
      return 1;
    }
    if (!databaseProbeSingleNativeHandle(path.c_str())) return 1;
    if ((mode & SQLITE_OPEN_READWRITE) && sqlite3_exec(alias,
        "PRAGMA journal_mode=OFF; PRAGMA synchronous=FULL; UPDATE sample SET value=value;",
        nullptr, nullptr, nullptr)) return 1;
    const int queried = sqlite3_exec(alias, "SELECT value FROM sample;", nullptr, nullptr, nullptr);
    std::string failure;
    if (!adapter.commitState(failure) ||
        sqlite3_exec(alias, "SELECT value FROM sample;", nullptr, nullptr, nullptr)) return 1;
    if (mode == SQLITE_OPEN_READONLY &&
        sqlite3_exec(alias, "UPDATE sample SET value='forbidden';", nullptr, nullptr, nullptr) != SQLITE_READONLY)
      return 1;
    const int closedAlias = sqlite3_close(alias);
    if (queried || closedAlias) return 1;
  }
  std::puts("same-file RW/RW and RW/RO opens: PASS");
  sqlite3_close(idle.handle);
  idle.handle = nullptr;
  {
    databaseProbeCheckpoint("Same-file read-first promotion and writer-first close");
    Connection reader, writer;
    if (sqlite3_open_v2(path.c_str(), &reader.handle, SQLITE_OPEN_READONLY, nullptr) ||
        sqlite3_exec(reader.handle, "SELECT value FROM sample;", nullptr, nullptr, nullptr) ||
        sqlite3_open_v2(path.c_str(), &writer.handle, SQLITE_OPEN_READWRITE, nullptr)) return 1;
    std::string failure;
    if (!adapter.commitState(failure)) return 1;
    sqlite3_close(writer.handle);
    writer.handle = nullptr;
    if (sqlite3_exec(reader.handle, "SELECT value FROM sample;", nullptr, nullptr, nullptr)) return 1;
  }
  if (sqlite3_open_v2(path.c_str(), &idle.handle, SQLITE_OPEN_READWRITE, nullptr) ||
      sqlite3_exec(idle.handle, "PRAGMA journal_mode=OFF;", nullptr, nullptr, nullptr)) return 1;
  std::puts("read-first upgrade, shared commit, close ordering, readonly enforcement: PASS");
  {
    void* outside = nullptr;
    const std::string sibling = state.string() + "-sibling/outside.db";
    errno = 0;
    if (!adapter.openNative(sibling.c_str(), false, &outside) || outside || errno != EACCES)
      return 1;
    std::puts("trailing-separator root accepts children and rejects sibling prefix: PASS");
  }

  resolved = resolveDatabaseOpenPath("file://external/missing.db",
      SQLITE_OPEN_READONLY, false, &readOnly);
  if (resolved || std::filesystem::exists(state / "missing.db")) return 1;
  std::puts("missing named database: PASS");

  // A genuine encrypted database created above is copied byte-for-byte into
  // the lower store with its original basename, then opened through copy-up.
  resolved = resolveDatabaseOpenPath("file://external/seed/contract.db",
      SQLITE_OPEN_READWRITE, false, &readOnly);
  if (!resolved) return 1;
  const std::string copied(resolved);
  delete[] resolved;
  db = nullptr;
  rc = sqlite3_open_v2(copied.c_str(), &db, SQLITE_OPEN_READWRITE, nullptr);
  if (!rc) rc = sqlite3_exec(db, "PRAGMA journal_mode=OFF; UPDATE sample SET value='copied';"
                            "UPDATE sample SET value='second'; UPDATE sample SET value='copied';",
                            nullptr, nullptr, nullptr);
  sqlite3_close(db);
  if (rc != SQLITE_OK) { std::printf("copy-up failed: %d\n", rc); return 1; }
  std::puts("encrypted copy-up and mutation: PASS");
  {
    Connection second;
    if (sqlite3_open_v2(copied.c_str(), &second.handle, SQLITE_OPEN_READWRITE, nullptr)) return 1;
    auto usable = [&] {
      return sqlite3_exec(idle.handle, "SELECT value FROM sample;", nullptr, nullptr, nullptr) == SQLITE_OK &&
          sqlite3_exec(second.handle, "SELECT value FROM sample;", nullptr, nullptr, nullptr) == SQLITE_OK;
    };
    databaseProbeCheckpoint("Expected rejection: raw commit with database writers open");
    std::string failure;
    if (commit(failure)) return 1;
    std::puts("uncoordinated archive commit rejected: PASS");

    // Exercise the actual runtime publication callback, not a duplicate model.
    std::ofstream download(state / "pending-probe-payload");
    download << "download bytes\n";
    download.close();
    const unsigned beforePublish = externalCommits;
    if (!download || platform.irename("file://state/pending-probe-payload",
        "file://state/published-probe-payload") != 0 ||
        externalCommits != beforePublish + 1 || !externalCommitOk || !usable()) return 1;
    std::puts("runtime file publication with two database writers: PASS");

    const unsigned beforeCookies = externalCommits;
    platform.clearCookies();
    if (externalCommits != beforeCookies + 1 || !externalCommitOk || !usable()) return 1;
    std::puts("runtime cookie-clear commit with two database writers: PASS");

    std::ofstream pendingFailure(state / "pending-failure-payload");
    pendingFailure << "failure test\n";
    pendingFailure.close();
    if (!pendingFailure) return 1;
    failCommit = true;
    const unsigned beforeFailedPublish = externalCommits;
    const int publishFailure = platform.irename("file://state/pending-failure-payload",
                                                "file://state/failed-publication-payload");
    const bool publicationReported = publishFailure != 0 && !externalCommitOk &&
        externalCommits == beforeFailedPublish + 1;
    const unsigned beforeFailedCookies = externalCommits;
    platform.clearCookies(); // void API: observe failure at its supplied callback.
    const bool cookiesReported = !externalCommitOk && externalCommits == beforeFailedCookies + 1;
    failCommit = false;
    if (!publicationReported || !cookiesReported || !usable() ||
        !adapter.commitState(failure)) return 1;
    std::puts("runtime publication/cookie callback failures and recovery: PASS");

    // This tests the storage safe point used by suspend, not actual HOME/sleep
    // or background-network cancellation. Those remain integration tests.
    if (!adapter.commitState(failure) || !usable()) return 1;
    std::puts("suspend-style quiescent storage commit: PASS");
    failCommit = true;
    const bool failed = !adapter.commitState(failure);
    failCommit = false;
    if (!failed || !usable() || !adapter.commitState(failure)) return 1;
    std::puts("external commit failure and retry preserve connections: PASS");

    FILE* active = std::fopen((state / "active-probe-writer").string().c_str(), "wb");
    if (!active) return 1;
    databaseProbeCheckpoint("Expected rejection: unrelated writer still open");
    const bool rejected = !adapter.commitState(failure);
    const int closeResult = std::fclose(active);
    if (!rejected || closeResult || !usable() || !adapter.commitState(failure)) return 1;
    std::puts("unclosed non-database writer rejected; closed-writer retry: PASS");

    databaseProbeCheckpoint("Worker commits while engine reads both databases");
    adapter.setTrace({}); // The Switch console belongs to the main thread.
    std::atomic<bool> workerOk{true};
    std::atomic<bool> started{false};
    std::thread worker([&] {
      started = true;
      for (unsigned i = 0; i < 16; ++i) {
        std::string error;
        if (!adapter.commitState(error)) { workerOk = false; break; }
        std::this_thread::yield();
      }
    });
    while (!started) std::this_thread::yield();
    bool readsOk = true;
    for (unsigned i = 0; i < 64; ++i) {
      sqlite3_db_release_memory(idle.handle);
      sqlite3_db_release_memory(second.handle);
      if (!usable()) { readsOk = false; break; }
    }
    worker.join();
    adapter.setTrace(databaseProbeCheckpoint);
    if (!workerOk || !readsOk) return 1;
    std::puts("worker archive commits versus database reads: PASS");
  }
  rc = sqlite3_exec(idle.handle, "INSERT INTO sample VALUES(2,'temporary'); DELETE FROM sample WHERE id=2;",
                    nullptr, nullptr, nullptr);
  if (rc) return 1;
  std::puts("multiple writable connections survive commit and copy-up: PASS");

  resolved = resolveDatabaseOpenPath("asset://seed/contract.db", SQLITE_OPEN_READONLY,
                                      false, &readOnly);
  if (!resolved || !readOnly) return 1;
  db = nullptr;
  rc = sqlite3_open_v2(resolved, &db, SQLITE_OPEN_READONLY, nullptr);
  delete[] resolved;
  statement = nullptr;
  if (!rc) rc = sqlite3_prepare_v2(db, "SELECT value FROM sample", -1, &statement, nullptr);
  const bool lowerUnchanged = !rc && sqlite3_step(statement) == SQLITE_ROW &&
      std::string(reinterpret_cast<const char*>(sqlite3_column_text(statement, 0))) == "persisted";
  sqlite3_finalize(statement);
  sqlite3_close(db);
  if (!lowerUnchanged) return 1;
  std::puts("read-only encrypted asset and unchanged lower database: PASS");

  failCommit = true;
  resolved = resolveDatabaseOpenPath("file://external/failure/contract.db",
                                     SQLITE_OPEN_READWRITE, false, &readOnly);
  if (resolved || std::filesystem::exists(state / "failure/contract.db")) return 1;
  failCommit = false;
  resolved = resolveDatabaseOpenPath("file://external/failure/contract.db",
                                     SQLITE_OPEN_READWRITE, false, &readOnly);
  if (!resolved) return 1;
  delete[] resolved;
  std::puts("failed copy-up does not publish; retry succeeds: PASS");

  // Confirm the original file really is ciphertext rather than accidentally
  // exercising plain SQLite. Also test long logical names through the codec.
  // The lower fixture is byte-identical and is not held by the idle writer.
  std::ifstream encrypted(content / "seed/contract.db", std::ios::binary);
  char prefix[16]{};
  encrypted.read(prefix, sizeof(prefix));
  if (!encrypted || !std::memcmp(prefix, "SQLite format 3", 15)) return 1;
  encrypted.close();
  std::puts("on-disk payload is encrypted: PASS");
  const std::string longUri = "file://state/" + std::string(70, 'a') + ".db";
  resolved = resolveDatabaseOpenPath(longUri.c_str(), createFlags, false, &readOnly);
  if (!resolved) return 1;
  const std::string longPath(resolved);
  delete[] resolved;
  db = nullptr;
  rc = sqlite3_open_v2(longPath.c_str(), &db, createFlags, nullptr);
  if (!rc) rc = sqlite3_exec(db,
      "PRAGMA journal_mode=OFF; CREATE TABLE t(v); INSERT INTO t VALUES(42);",
      nullptr, nullptr, nullptr);
  sqlite3_close(db);
  if (rc) return 1;
  db = nullptr;
  rc = sqlite3_open_v2(longPath.c_str(), &db, SQLITE_OPEN_READONLY, nullptr);
  statement = nullptr;
  if (!rc) rc = sqlite3_prepare_v2(db, "SELECT v FROM t", -1, &statement, nullptr);
  const bool longMatched = !rc && sqlite3_step(statement) == SQLITE_ROW &&
                           sqlite3_column_int(statement, 0) == 42;
  sqlite3_finalize(statement);
  sqlite3_close(db);
  if (!longMatched) return 1;
  std::puts("encoded long filename retains its encryption key: PASS");

  for (const char* invalid : {"file://state/../escape.db", "file://external//escape.db",
                             "file://state/.pgdb-copy"}) {
    resolved = resolveDatabaseOpenPath(invalid, createFlags, false, &readOnly);
    if (resolved) { delete[] resolved; return 1; }
  }
  std::puts("invalid paths rejected: PASS");

  db = nullptr;
  rc = sqlite3_open_v2(copied.c_str(), &db, SQLITE_OPEN_READWRITE, nullptr);
  if (!rc) rc = sqlite3_exec(db, "PRAGMA journal_mode=OFF; PRAGMA synchronous=FULL;",
                            nullptr, nullptr, nullptr);
  failCommit = true;
  if (!rc) rc = sqlite3_exec(db, "UPDATE sample SET value='must-report-failure';",
                            nullptr, nullptr, nullptr);
  const int extended = sqlite3_extended_errcode(db);
  sqlite3_close(db);
  if (rc != SQLITE_IOERR || extended != SQLITE_IOERR_FSYNC) {
    std::printf("commit failure hidden: rc=%d extended=%d\n", rc, extended); return 1;
  }
  std::puts("commit failure propagates as SQLITE_IOERR_FSYNC: PASS");

  // A commit failure must restore the other connection too, not strand it
  // with a closed FILE*. Its logical and encryption state remain intact.
  failCommit = false;
  rc = sqlite3_exec(idle.handle, "INSERT INTO sample VALUES(2,'after failure'); DELETE FROM sample WHERE id=2;",
                   nullptr, nullptr, nullptr);
  if (rc) return 1;
  std::puts("other connection remains usable after failed commit: PASS");

  {
    // Deferred mode, as used by the game host: transactions mark SaveData
    // pending; the background boundary commits content first, then state.
    unsigned before = 0;
    bool failBefore = false;
    adapter.setBeforeCommit([&](std::string& error) {
      ++before;
      if (failBefore) { error = "injected content commit failure"; return false; }
      return true;
    });
    adapter.setDeferredCommit(true);
    std::string failure;
    const unsigned startCommits = commits;
    for (unsigned i = 0; i < 8; ++i)
      if (sqlite3_exec(idle.handle, "INSERT INTO sample VALUES(3,'deferred'); DELETE FROM sample WHERE id=3;",
                       nullptr, nullptr, nullptr)) return 1;
    if (commits != startCommits || !adapter.commitPendingState()) return 1;
    adapter.written(nullptr); // Model a page write after the last xSync.
    if (!adapter.commitPending(failure) || commits != startCommits) return 1;
    if (sqlite3_exec(idle.handle, "UPDATE sample SET value=value;", nullptr, nullptr, nullptr)) return 1;
    failBefore = true;
    if (adapter.commitPending(failure) || commits != startCommits || !adapter.commitPendingState() ||
        sqlite3_exec(idle.handle, "SELECT value FROM sample;", nullptr, nullptr, nullptr)) return 1;
    failBefore = false;
    const unsigned beforeCalls = before;
    if (!adapter.commitPending(failure) || commits != startCommits + 1 || before != beforeCalls + 1 ||
        adapter.commitPendingState()) return 1;
    if (!adapter.commitPending(failure) || commits != startCommits + 1) return 1;
    adapter.setDeferredCommit(false);
    adapter.setBeforeCommit({});
    std::puts("deferred SaveData: coalesced, transaction-boundary only, content-first, retry: PASS");
  }

  db = nullptr;
  rc = sqlite3_open_v2(longPath.c_str(), &db, SQLITE_OPEN_READWRITE, nullptr);
  if (!rc) rc = sqlite3_exec(db, "PRAGMA journal_mode=OFF; PRAGMA synchronous=FULL;",
                            nullptr, nullptr, nullptr);
  // Inject disappearance after the archive commit, before native reopen.
  interruptRestore = longPath;
  if (!rc) rc = sqlite3_exec(db, "UPDATE t SET v=43;", nullptr, nullptr, nullptr);
  const int restoreError = sqlite3_extended_errcode(db);
  sqlite3_close(db);
  if (rc != SQLITE_IOERR || restoreError != SQLITE_IOERR_FSYNC ||
      !interruptRestore.empty() || std::filesystem::exists(longPath)) return 1;
  std::puts("failed stream restore reports I/O error without recreating file: PASS");
  return 0;
}
