#pragma once
#include "Playground/Runtime/StorageWriter.h"
#include <cerrno>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <functional>
#include <filesystem>
#include <map>
#include <mutex>
#include <set>
#include <thread>
#include <unistd.h>

namespace playground::switch_runtime {
// CacheStorage commits require every native writer in the archive to close.
// Download/extraction streams retain their logical lifetime and byte position
// while native handles are suspended. Native IO and commit are serialized,
// matching the package-batched hardware baseline. Snapshot metadata has a
// separate short-held lock so diagnostics never wait for filesystem IO.
class SwitchArchiveWriters {
  using Clock = std::chrono::steady_clock;
  static double seconds(Clock::time_point start) {
    return std::chrono::duration<double>(Clock::now() - start).count();
  }
  static unsigned long long nowNs() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch()).count();
  }
  struct File {
    // Newlib's default buffer is only 1 KiB. Network callbacks and extraction
    // write synchronously, so use bounded per-stream buffering to amortize
    // filesystem calls. Commit/close still flush all bytes before publication.
    std::array<char, 64 * 1024> buffer;
    FILE* stream{};
    long offset{};
    bool failed{};
    std::thread::id owner;
    bool configureBuffer() {
      return std::setvbuf(stream, buffer.data(), _IOFBF, buffer.size()) == 0;
    }
  };
  struct State {
    std::mutex archive;
    // Registry, errors and counters only: never held across filesystem IO.
    std::mutex mutex;
    std::map<std::string, File*> files;
    std::set<std::thread::id> batches;
    std::function<bool(std::string&)> commit;
    std::string root, error;
    std::atomic<bool> poisoned{false};
    std::atomic<unsigned> operation{0}, waiters{0};
    std::atomic<unsigned long long> operationSince{0};
    unsigned long long writeCalls{}, writeBytes{}, commitCalls{}, commitFailures{};
    // Journal usage since the last successful commit. With a policy set,
    // publication requests are deferred until one of these bounds is reached.
    unsigned long long pendingBytes{}, pendingOps{}, maxPendingBytes{}, maxPendingOps{};
    unsigned long long deferredRequests{};
    double writeSeconds{}, waitSeconds{}, closeSeconds{}, commitSeconds{}, openSeconds{};
    double maxWriteSeconds{}, maxCloseSeconds{}, maxCommitSeconds{};
    void fail(const std::string& operation, const std::string& path) {
      const int saved = errno;
      std::lock_guard<std::mutex> lock(mutex);
      if (!error.empty()) error += "; ";
      error += operation + " " + path + ": " + std::strerror(saved);
      poisoned = true;
    }
  };
  struct ReadGate {
    State& state;
    std::unique_lock<std::mutex> lock;
    explicit ReadGate(State& state, unsigned operation = 1)
        : state(state), lock(state.archive, std::defer_lock) {
      ++state.waiters;
      lock.lock();
      --state.waiters;
      state.operationSince = nowNs();
      state.operation = operation;
    }
    ~ReadGate() { release(); }
    void release() {
      if (!lock.owns_lock()) return;
      state.operation = 0;
      lock.unlock();
    }
  };
  static bool commitArchive(State& state, std::string& error);
  class Writer final : public runtime::StorageWriter {
  public:
    Writer(std::shared_ptr<State> state, std::string path)
        : m_state(std::move(state)), m_path(std::move(path)) {}
    ~Writer() override { close(); }
    std::size_t write(const void* data, std::size_t size) override {
      const auto waiting = Clock::now();
      ReadGate gate(*m_state, 2);
      const double waited = seconds(waiting);
      if (!file.stream || m_state->poisoned || file.failed) { errno = EIO; return 0; }
      const auto writing = Clock::now();
      const auto written = std::fwrite(data, 1, size, file.stream);
      const double elapsed = seconds(writing);
      {
        std::lock_guard<std::mutex> stats(m_state->mutex);
        m_state->waitSeconds += waited;
        m_state->writeSeconds += elapsed;
        m_state->maxWriteSeconds = std::max(m_state->maxWriteSeconds, elapsed);
        ++m_state->writeCalls;
        m_state->writeBytes += written;
      }
      if (written != size) {
        file.failed = true;
        m_state->fail("write", m_path);
      }
      bool full = false;
      {
        std::lock_guard<std::mutex> stats(m_state->mutex);
        m_state->pendingBytes += written;
        full = m_state->maxPendingBytes && m_state->pendingBytes >= m_state->maxPendingBytes;
      }
      gate.release();
      if (full) {
        // Bound uncommitted journal data even inside one large download.
        std::string error;
        if (!commitArchive(*m_state, error))
          std::fprintf(stderr, "CacheStorage journal-bound commit failed: %s\n", error.c_str());
      }
      return written;
    }
    long position() override {
      ReadGate gate(*m_state);
      return file.stream && !m_state->poisoned ? std::ftell(file.stream) : -1;
    }
    int close() override {
      ReadGate gate(*m_state, 3);
      if (m_closed) return m_result;
      m_closed = true;
      m_result = file.failed || m_state->poisoned ? -1 : 0;
      const auto closing = Clock::now();
      if (file.stream && std::fclose(file.stream) != 0) {
        m_state->fail("close", m_path);
        m_result = -1;
      }
      const double elapsed = seconds(closing);
      file.stream = nullptr;
      std::lock_guard<std::mutex> stats(m_state->mutex);
      m_state->closeSeconds += elapsed;
      m_state->maxCloseSeconds = std::max(m_state->maxCloseSeconds, elapsed);
      auto found = m_state->files.find(m_path);
      if (found != m_state->files.end() && found->second == &file)
        m_state->files.erase(found);
      return m_result;
    }
    File file;
  private:
    std::shared_ptr<State> m_state;
    std::string m_path;
    bool m_closed{};
    int m_result{};
  };
public:
  SwitchArchiveWriters(std::string root, std::function<bool(std::string&)> commit)
      : m_state(std::make_shared<State>()) {
    root = std::filesystem::path(root).lexically_normal().generic_string();
    if (root.empty() || root.back() != '/') root += '/';
    m_state->root = std::move(root);
    m_state->commit = std::move(commit);
  }
  std::unique_ptr<runtime::StorageWriter> open(const std::string& input) {
    const std::string path = std::filesystem::path(input).lexically_normal().generic_string();
    // Allocate before acquiring the mutex: failed opens must destroy the writer
    // only after releasing it, since its destructor uses the same mutex.
    auto writer = std::make_unique<Writer>(m_state, path);
    ReadGate gate(*m_state, 4);
    if (path.compare(0, m_state->root.size(), m_state->root) != 0) { errno = EACCES; return nullptr; }
    if (m_state->poisoned) { errno = EIO; return nullptr; }
    {
      std::lock_guard<std::mutex> lock(m_state->mutex);
      if (m_state->files.count(path)) { errno = EBUSY; return nullptr; }
      // Reserve before fopen so concurrent aliases cannot truncate one another.
      // Writer's destructor removes the reservation even if fopen fails.
      writer->file.owner = std::this_thread::get_id();
      m_state->files.emplace(path, &writer->file);
    }
    // Failed opens must leave no null native handle in the registry during
    // the gap between releasing the archive gate and destroying the writer.
    const auto unreserve = [&] {
      std::lock_guard<std::mutex> lock(m_state->mutex);
      m_state->files.erase(path);
    };
    const auto opening = Clock::now();
    writer->file.stream = std::fopen(path.c_str(), "wb");
    {
      std::lock_guard<std::mutex> stats(m_state->mutex);
      m_state->openSeconds += seconds(opening);
    }
    if (!writer->file.stream) { unreserve(); return nullptr; }
    if (!writer->file.configureBuffer()) {
      std::fclose(writer->file.stream);
      writer->file.stream = nullptr;
      unreserve();
      errno = ENOMEM;
      return nullptr;
    }
    return writer;
  }
  bool beginBatch(std::string& error) {
    ReadGate gate(*m_state);
    std::lock_guard<std::mutex> lock(m_state->mutex);
    if (m_state->poisoned) { error = m_state->error; return false; }
    if (!m_state->batches.insert(std::this_thread::get_id()).second) {
      error = "Nested archive write batches are unsupported";
      return false;
    }
    return true;
  }
  bool endBatch(bool success, std::string& error) {
    {
      ReadGate gate(*m_state);
      std::lock_guard<std::mutex> lock(m_state->mutex);
      const auto thread = std::this_thread::get_id();
      if (!m_state->batches.erase(thread)) {
        error = "Archive write batch ended without a matching begin";
        return false;
      }
      if (!success) return true; // Failed extraction is never reported durable.
      for (const auto& entry : m_state->files) {
        if (entry.second->owner == thread) {
          error = "Archive write batch ended with an unclosed output stream";
          return false;
        }
      }
    }
    // Complete the durability boundary before the extractor can report success.
    return commit(error);
  }
  bool requestCommit(std::string& error) {
    {
      ReadGate gate(*m_state);
      std::lock_guard<std::mutex> lock(m_state->mutex);
      if (m_state->poisoned) { error = m_state->error; return false; }
      ++m_state->pendingOps;
      const bool bounded = m_state->maxPendingOps || m_state->maxPendingBytes;
      const bool full =
          (m_state->maxPendingOps && m_state->pendingOps >= m_state->maxPendingOps) ||
          (m_state->maxPendingBytes && m_state->pendingBytes >= m_state->maxPendingBytes);
      // Without a policy, batches defer unconditionally (historic behavior).
      // With one, even a batch commits before exceeding the journal bounds.
      if (!full && (bounded || m_state->batches.count(std::this_thread::get_id()))) {
        ++m_state->deferredRequests;
        return true;
      }
    }
    return commit(error);
  }
  // Explicit lifecycle commits are never deferred, even on a batching thread.
  bool commit(std::string& error) { return commitArchive(*m_state, error); }
  // Commit only when something was written or published since the last commit.
  bool commitIfDirty(std::string& error) {
    {
      std::lock_guard<std::mutex> lock(m_state->mutex);
      if (!m_state->pendingBytes && !m_state->pendingOps && !m_state->poisoned) return true;
    }
    return commit(error);
  }
  // Zero bounds (the default) commit on every publication request.
  void setCommitPolicy(unsigned long long maxBytes, unsigned long long maxOps) {
    std::lock_guard<std::mutex> lock(m_state->mutex);
    m_state->maxPendingBytes = maxBytes;
    m_state->maxPendingOps = maxOps;
  }
  std::string profile() {
    std::lock_guard<std::mutex> lock(m_state->mutex);
    char text[1024];
    const unsigned operation = m_state->operation.load();
    const auto since = m_state->operationSince.load();
    const auto now = nowNs();
    const double age = operation && now >= since ? (now - since) / 1e9 : 0;
    std::snprintf(text, sizeof(text),
        "Storage totals (downloads + extraction)\n"
        "Buffer: 64 KiB; writes=%llu; bytes=%llu\nWrite: %.3f s\n"
        "Write-lock wait: %.3f s; open: %.3f s; close: %.3f s\nCommits=%llu; failures=%llu\n"
        "Commit: %.3f s; active writers=%zu; ZIP batches=%zu\n"
        "Longest write/close/commit: %.3f / %.3f / %.3f s\n"
        "IO phase=%u age=%.3f s waiters=%u (0 idle,1 control,2 write,3 close,4 open,5 commit)\n"
        "Deferred requests=%llu; pending bytes=%llu ops=%llu (bounds %llu / %llu)\n",
        m_state->writeCalls, m_state->writeBytes, m_state->writeSeconds,
        m_state->waitSeconds, m_state->openSeconds, m_state->closeSeconds, m_state->commitCalls, m_state->commitFailures,
        m_state->commitSeconds, m_state->files.size(), m_state->batches.size(),
        m_state->maxWriteSeconds, m_state->maxCloseSeconds, m_state->maxCommitSeconds,
        operation, age, m_state->waiters.load(),
        m_state->deferredRequests, m_state->pendingBytes, m_state->pendingOps,
        m_state->maxPendingBytes, m_state->maxPendingOps);
    return text;
  }
private:
  std::shared_ptr<State> m_state;
};
inline bool SwitchArchiveWriters::commitArchive(State& state, std::string& error) {
  ReadGate gate(state, 5);
  const auto started = Clock::now();
  bool success = false;
  struct Record {
    State& state;
    Clock::time_point started;
    bool& success;
    ~Record() {
      std::lock_guard<std::mutex> stats(state.mutex);
      const double elapsed = seconds(started);
      ++state.commitCalls;
      state.commitSeconds += elapsed;
      state.maxCommitSeconds = std::max(state.maxCommitSeconds, elapsed);
      if (!success) ++state.commitFailures;
    }
  } record{state, started, success};
  if (state.poisoned) { error = state.error; return false; }
  state.error.clear();
  for (const auto& entry : state.files) {
    auto& file = *entry.second;
    if (!file.stream || (file.offset = std::ftell(file.stream)) < 0 ||
        std::fflush(file.stream) != 0 || fsync(fileno(file.stream)) != 0) {
      state.fail("flush/position", entry.first);
      error = state.error;
      return false;
    }
  }
  for (const auto& entry : state.files) {
    auto& file = *entry.second;
    if (std::fclose(file.stream) != 0) state.fail("suspend", entry.first);
    file.stream = nullptr;
  }
  const bool committed = !state.poisoned && state.commit(state.error);
  // Even a failed archive commit restores handles so callers can retry or
  // unwind. Never recreate/truncate a file that disappeared during commit.
  for (const auto& entry : state.files) {
    auto& file = *entry.second;
    file.stream = std::fopen(entry.first.c_str(), "r+b");
    if (!file.stream || !file.configureBuffer() ||
        std::fseek(file.stream, file.offset, SEEK_SET) != 0) {
      state.fail("restore", entry.first);
      if (file.stream) std::fclose(file.stream);
      file.stream = nullptr;
    }
  }
  error = state.error;
  success = committed && !state.poisoned;
  if (success) {
    std::lock_guard<std::mutex> lock(state.mutex);
    state.pendingBytes = 0;
    state.pendingOps = 0;
  }
  return success;
}
}
