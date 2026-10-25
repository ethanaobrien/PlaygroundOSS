#ifndef PLAYGROUND_SWITCH_DATABASE_H
#define PLAYGROUND_SWITCH_DATABASE_H

#include "DatabasePlatform.h"
#include <atomic>
#include <filesystem>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace playground::switch_runtime {

// The same implementation is exercised by the host conformance test. The host
// supplies mounted roots and commit/error functions; no game data is known here.
class SwitchDatabase final : public DatabasePlatform {
public:
  using Commit = std::function<bool(std::string&)>;
  SwitchDatabase(std::filesystem::path install, std::filesystem::path content,
                 std::filesystem::path state, bool encoded, Commit commit);
  ~SwitchDatabase() override;
  bool initialize();
  // Serialized commit for any SaveData publisher, not just SQLite.
  // Non-database writers must already be closed and background work quiesced.
  bool commitState(std::string& error);
  // Runs before every SaveData commit, so state never becomes durable while
  // content it may reference is still only in the CacheStorage journal.
  void setBeforeCommit(Commit before) { m_before = std::move(before); }
  // Deferred mode: xSync records a consistent point instead of committing.
  // commitPending() then commits at most once per call, and only when no
  // SQLite write has happened since the last xSync (journal_mode=OFF means a
  // mid-transaction archive commit would persist a partial transaction).
  void setDeferredCommit(bool deferred) { m_deferred = deferred; }
  bool commitPending(std::string& error);
  bool commitPendingState() const { return m_pending; }
  void written(void*) override { m_unsynced = true; }
  void lockIO() override;
  void unlockIO() override;
  // Diagnostics: database IO time on the engine thread versus all threads.
  void setMainThread(std::thread::id id) { m_mainThread = id; }
  std::string profile() const;
  void setNativeError(std::function<std::string()> describe) { m_nativeError = std::move(describe); }
  void recordFileError(const char*, int) override;
  void setTrace(std::function<void(const char*)> trace) { m_trace = std::move(trace); }
  const char* resolve(const char*, int, bool, bool&) override;
  int sync(const char*, void*) override;
  bool openNative(const char*, bool, void**) override;
  void closing(void**) override;
  const char* error() const override { return m_error.c_str(); }
  const char* keyPath(const char*) const override;
private:
  std::recursive_mutex m_ioMutex;
  std::thread::id m_mainThread;
  std::atomic<unsigned long long> m_mainWaitNs{0}, m_mainHeldNs{0}, m_mainOps{0},
      m_heldNs{0}, m_ops{0}, m_syncs{0}, m_commits{0}, m_commitNs{0}, m_maxMainNs{0};
  std::function<std::string()> m_nativeError;
  std::filesystem::path physical(const std::filesystem::path&) const;
  bool copyUp(const std::filesystem::path&, const std::filesystem::path&);
  bool commitWithClosedWriters();
  void trace(const char* text) const { if (m_trace) m_trace(text); }
  std::function<void(const char*)> m_trace;
  struct NativeFile {
    void* stream{};
    bool writable{};
    long position{};
    std::vector<void**> slots;
    void publish(void* value) {
      stream = value;
      for (auto slot : slots) *slot = value;
    }
  };
  std::map<std::string, NativeFile> m_files;
  bool m_ioFailed{};
  Commit m_before;
  bool m_deferred{};
  std::atomic<bool> m_pending{false};
  std::atomic<bool> m_unsynced{false};
  std::filesystem::path m_install, m_content, m_state;
  bool m_encoded;
  Commit m_commit;
  std::string m_error;
  std::map<std::string, std::string> m_keys;
  sqlite3_vfs m_vfs{};
  sqlite3_vfs* m_base{};
  bool m_registered{};
};
}
#endif
