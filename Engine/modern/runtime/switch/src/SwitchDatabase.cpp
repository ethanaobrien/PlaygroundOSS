#include "Playground/Switch/SwitchDatabase.h"
#include "Playground/Switch/SwitchPath.h"
#include "Playground/Switch/SwitchFileCopy.h"
#include <array>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <algorithm>
#include <chrono>
#include <unistd.h>

namespace playground::switch_runtime {
namespace {
std::uint64_t monotonicNs() {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count();
}
// Outermost lock depth and acquisition time per thread (the mutex is recursive).
thread_local unsigned tDepth = 0;
thread_local std::uint64_t tAcquiredNs = 0;
int absolutePath(sqlite3_vfs*, const char* name, int size, char* output) {
  // Resolution has already produced an absolute host or libnx device path.
  // Do not let the Unix VFS prepend its cwd to a Horizon mount name.
  if (!name || (!(*name == '/') && !std::strstr(name, ":/")) ||
      std::strlen(name) >= static_cast<std::size_t>(size)) return SQLITE_CANTOPEN;
  std::strcpy(output, name);
  return SQLITE_OK;
}
bool exists(const std::filesystem::path& path, bool& result, std::string& error) {
  std::error_code ec;
  result = std::filesystem::exists(path, ec);
  if (ec) error = "stat " + path.string() + ": " + ec.message();
  return !ec;
}
}
SwitchDatabase::SwitchDatabase(std::filesystem::path install,
    std::filesystem::path content, std::filesystem::path state,
    bool encoded, Commit commit)
    : m_install(std::move(install)), m_content(std::move(content)),
      m_state(std::move(state)), m_encoded(encoded), m_commit(std::move(commit)) {}

SwitchDatabase::~SwitchDatabase() {
  if (databasePlatform() == this && !registerDatabasePlatform(nullptr)) {
    std::fputs("Database adapter destroyed with open connections\n", stderr);
    std::abort();
  }
  if (m_registered) {
    sqlite3_vfs_unregister(&m_vfs);
    sqlite3_vfs_register(m_base, 1);
  }
}
void SwitchDatabase::lockIO() {
  if (tDepth++) { m_ioMutex.lock(); return; }
  const auto requested = monotonicNs();
  m_ioMutex.lock();
  tAcquiredNs = monotonicNs();
  if (std::this_thread::get_id() == m_mainThread) m_mainWaitNs += tAcquiredNs - requested;
}
void SwitchDatabase::unlockIO() {
  if (--tDepth == 0) {
    const auto held = monotonicNs() - tAcquiredNs;
    m_heldNs += held;
    ++m_ops;
    if (std::this_thread::get_id() == m_mainThread) {
      m_mainHeldNs += held;
      ++m_mainOps;
      auto previous = m_maxMainNs.load();
      while (previous < held && !m_maxMainNs.compare_exchange_weak(previous, held)) {}
    }
  }
  m_ioMutex.unlock();
}
std::string SwitchDatabase::profile() const {
  char text[384];
  std::snprintf(text, sizeof(text),
      "Database: ops=%llu held=%.3f s; engine thread ops=%llu held=%.3f s wait=%.3f s longest=%.1f ms\n"
      "Database: syncs=%llu SaveData commits=%llu (%.3f s) pending=%d\n",
      m_ops.load(), m_heldNs.load() / 1e9, m_mainOps.load(), m_mainHeldNs.load() / 1e9,
      m_mainWaitNs.load() / 1e9, m_maxMainNs.load() / 1e6,
      m_syncs.load(), m_commits.load(), m_commitNs.load() / 1e9, m_pending.load() ? 1 : 0);
  return text;
}
bool SwitchDatabase::initialize() {
  if (m_registered || !registerDatabasePlatform(this)) {
    m_error = "Database adapter must register once before encryption initialization";
    return false;
  }
  if (sqlite3_initialize() != SQLITE_OK ||
      !(m_base = sqlite3_vfs_find("unix-none"))) {
    m_error = "SQLite unix-none VFS unavailable";
    registerDatabasePlatform(nullptr);
    return false;
  }
  m_vfs = *m_base;
  m_vfs.pNext = nullptr;
  m_vfs.zName = "playground-switch-database";
  m_vfs.xFullPathname = absolutePath;
  if (sqlite3_vfs_register(&m_vfs, 1) != SQLITE_OK) {
    m_error = "Could not register database VFS";
    registerDatabasePlatform(nullptr);
    return false;
  }
  m_registered = true;
  return true;
}
std::filesystem::path SwitchDatabase::physical(const std::filesystem::path& path) const {
  return m_encoded ? encodeStoragePath(path) : path;
}
const char* SwitchDatabase::keyPath(const char* native) const {
  const auto it = m_keys.find(native);
  return it == m_keys.end() ? native : it->second.c_str();
}
bool SwitchDatabase::copyUp(const std::filesystem::path& source,
                            const std::filesystem::path& target) {
  // All database opens run on the engine thread. One reserved sibling stage
  // keeps individual components below Horizon's limit, even for encoded names.
  const auto temporary = target.parent_path() / ".pgdb-copy";
  std::error_code ec;
  std::filesystem::remove(temporary, ec);
  if (ec) { m_error = "Copy-up: " + ec.message(); return false; }
  if (!copyFileBytes(source.string(), temporary.string(), m_error)) return false;
  // Verify the complete physical ciphertext, without decrypting/re-encrypting.
  FILE* a = std::fopen(source.string().c_str(), "rb");
  FILE* b = std::fopen(temporary.string().c_str(), "rb");
  bool equal = a && b;
  std::array<unsigned char, 16384> left{}, right{};
  while (equal) {
    const auto n = std::fread(left.data(), 1, left.size(), a);
    const auto m = std::fread(right.data(), 1, right.size(), b);
    equal = n == m && !std::memcmp(left.data(), right.data(), n) &&
            !std::ferror(a) && !std::ferror(b);
    if (n < left.size()) break;
  }
  if (a && std::fclose(a)) equal = false;
  if (b && std::fclose(b)) equal = false;
  if (!equal) { m_error = "Copy-up verification failed"; return false; }
  if (!commitWithClosedWriters()) return false;
  std::filesystem::rename(temporary, target, ec);
  if (ec) { m_error = "Copy-up publication: " + ec.message(); return false; }
  return commitWithClosedWriters();
}
const char* SwitchDatabase::resolve(const char* uri, int flags,
                                   bool promote, bool& readOnly) {
  m_error.clear();
  if (!uri || !*uri) { m_error = "Unnamed database unsupported"; return nullptr; }
  const std::string name(uri);
  std::string relative;
  enum class Space { State, External, Asset, Install } space;
  if (name.compare(0, 13, "file://state/") == 0) {
    space = Space::State; relative = name.substr(13);
  } else if (name.compare(0, 16, "file://external/") == 0) {
    space = Space::External; relative = name.substr(16);
  } else if (name.compare(0, 8, "asset://") == 0) {
    space = Space::Asset; relative = name.substr(8);
  } else if (name.compare(0, 13, "file://asset/") == 0) {
    space = Space::Asset; relative = name.substr(13);
  } else if (name.compare(0, 15, "file://install/") == 0) {
    space = Space::Install; relative = name.substr(15);
  } else { m_error = "Unsupported database namespace"; return nullptr; }
  if (relative.empty() || relative.front() == '/' ||
      relative.find(':') != std::string::npos || relative.find('\\') != std::string::npos) {
    m_error = "Invalid relative database path"; return nullptr;
  }
  for (const auto& component : std::filesystem::path(relative)) {
    if (component == ".." || component == "." || component == ".pgdb-copy") {
      m_error = "Invalid or reserved database component"; return nullptr;
    }
  }
  readOnly = space == Space::Asset || space == Space::Install;
  const bool writable = !readOnly && (promote || (flags & SQLITE_OPEN_READWRITE));
  if (readOnly && !(flags & SQLITE_OPEN_READONLY)) {
    m_error = "Write requested for asset database"; return nullptr;
  }
  auto target = physical((readOnly ? m_install : m_state) / relative);
  bool present = false;
  if (!exists(target, present, m_error)) return nullptr;
  if (space == Space::Asset || (space == Space::External && !present)) {
    const auto lower = physical(m_content / relative);
    bool lowerPresent = false;
    if (!exists(lower, lowerPresent, m_error)) return nullptr;
    if (lowerPresent) {
      if (writable) {
        std::error_code ec;
        std::filesystem::create_directories(target.parent_path(), ec);
        if (ec) { m_error = ec.message(); return nullptr; }
        if (!copyUp(lower, target)) return nullptr;
      } else target = lower;
      present = true;
    }
  }
  if (!present && (!writable || (!promote && !(flags & SQLITE_OPEN_CREATE)))) {
    m_error = "Named database does not exist: " + target.string(); return nullptr;
  }
  if (writable) {
    std::error_code ec;
    std::filesystem::create_directories(target.parent_path(), ec);
    if (ec) { m_error = ec.message(); return nullptr; }
  }
  const std::string native = target.string();
  m_keys[native] = relative;
  char* result = new char[native.size() + 1];
  std::strcpy(result, native.c_str());
  return result;
}
bool SwitchDatabase::openNative(const char* path, bool readOnly, void** slot) {
  *slot = nullptr;
  std::string root = physical(m_state).generic_string();
  if (root.empty() || root.back() != '/') root += '/';
  if (!path || (!readOnly && std::strncmp(path, root.c_str(), root.size()) != 0)) {
    errno = EACCES;
    m_error = "Writable database outside account SaveData";
    return true;
  }
  if (m_ioFailed) { errno = EIO; return true; }
  auto found = m_files.find(path);
  if (found != m_files.end()) {
    auto& file = found->second;
    // Horizon forbids opening a writer beside another handle to the same file.
    // Upgrade a read-first handle only after closing it; logical readers retain
    // their own SQLite permissions and cipher state. Every operation seeks.
    if (!readOnly && !file.writable) {
      if (std::fclose(static_cast<FILE*>(file.stream)) != 0) {
        file.publish(nullptr);
        m_ioFailed = true;
        return true;
      }
      file.publish(nullptr);
      FILE* upgraded = std::fopen(path, "r+b");
      if (!upgraded) {
        const int failure = errno;
        file.publish(std::fopen(path, "rb"));
        if (!file.stream) m_ioFailed = true;
        errno = failure;
        return true;
      }
      file.writable = true;
      file.publish(upgraded);
    }
    file.slots.push_back(slot);
    *slot = file.stream;
    return true;
  }
  FILE* stream = std::fopen(path, readOnly ? "rb" : "r+b");
  if (stream) {
    NativeFile file;
    file.stream = stream;
    file.writable = !readOnly;
    file.slots.push_back(slot);
    m_files.emplace(path, std::move(file));
    *slot = stream;
  }
  return true;
}
void SwitchDatabase::closing(void** slot) {
  for (auto it = m_files.begin(); it != m_files.end(); ++it) {
    auto& file = it->second;
    auto found = std::find(file.slots.begin(), file.slots.end(), slot);
    if (found == file.slots.end()) continue;
    file.slots.erase(found);
    *slot = nullptr;
    if (file.slots.empty()) {
      if (file.stream && std::fclose(static_cast<FILE*>(file.stream)) != 0)
        m_ioFailed = true;
      m_files.erase(it);
    }
    return;
  }
}
bool SwitchDatabase::commitWithClosedWriters() {
  if (m_ioFailed) {
    m_error = "Database stream recovery failed; restart required";
    return false;
  }
  m_error.clear();
  // Commit replaceable content first. A failure leaves every stream open and
  // SaveData uncommitted, so the caller may retry without restoration.
  if (m_before && !m_before(m_error)) {
    if (m_error.empty()) m_error = "CacheStorage commit before SaveData failed";
    return false;
  }
  // SaveData commits are archive-wide, not file-wide. A second idle SQLite
  // connection is enough to make Horizon reject a commit unless it too closes.
  // Do not reconstruct SQLite/cipher objects; only suspend the native streams.
  trace("Commit: flush writable streams");
  for (auto& entry : m_files) {
    if (!entry.second.writable) continue;
    FILE* stream = static_cast<FILE*>(entry.second.stream);
    if (!stream || (entry.second.position = std::ftell(stream)) < 0 ||
        std::fflush(stream) != 0 || fsync(fileno(stream)) != 0) {
      m_error = "Database flush/position failed: " + entry.first + ": " + std::strerror(errno);
      m_ioFailed = true;
      return false; // Never commit after failed I/O.
    }
  }
  trace("Commit: close writable streams");
  for (auto& entry : m_files) {
    if (!entry.second.writable) continue;
    const int rc = std::fclose(static_cast<FILE*>(entry.second.stream));
    entry.second.publish(nullptr); // fclose consumes the stream even on failure.
    if (rc != 0) {
      m_error = "Database close failed: " + entry.first + ": " + std::strerror(errno);
      m_ioFailed = true;
    }
  }
  trace("Commit: call archive commit");
  const auto commitStarted = monotonicNs();
  const bool committed = !m_ioFailed && m_commit(m_error);
  m_commitNs += monotonicNs() - commitStarted;
  ++m_commits;
  trace("Commit: archive call returned");
  // Restore even after commit failure, so SQLite can unwind normally. Never
  // use a creation mode: a missing file here must not become an empty database.
  trace("Commit: restore writable streams");
  for (auto& entry : m_files) {
    if (!entry.second.writable) continue;
    FILE* stream = std::fopen(entry.first.c_str(), "r+b");
    if (!stream || std::fseek(stream, entry.second.position, SEEK_SET) != 0) {
      const int failure = errno;
      if (stream) std::fclose(stream);
      if (!m_error.empty()) m_error += "; ";
      m_error += "Database restore failed: " + entry.first + ": " + std::strerror(failure);
      m_ioFailed = true;
    } else entry.second.publish(stream);
  }
  trace("Commit: stream restoration finished");
  if (committed && !m_ioFailed) {
    m_pending = false;
    m_unsynced = false;
    return true;
  }
  return false;
}
int SwitchDatabase::sync(const char* path, void* stream) {
  bool registered = false;
  for (const auto& entry : m_files)
    if (entry.second.writable && entry.second.stream == stream && entry.first == path) registered = true;
  if (!registered) {
    m_error = "Sync of an unregistered writable database";
    return SQLITE_IOERR_FSYNC;
  }
  ++m_syncs;
  if (m_deferred) {
    if (m_ioFailed) {
      m_error = "Database stream recovery failed; restart required";
      return SQLITE_IOERR_FSYNC;
    }
    m_pending = true;
    m_unsynced = false;
    return SQLITE_OK;
  }
  return commitWithClosedWriters() ? SQLITE_OK : SQLITE_IOERR_FSYNC;
}
bool SwitchDatabase::commitState(std::string& error) {
  std::lock_guard<std::recursive_mutex> lock(m_ioMutex);
  const bool ok = commitWithClosedWriters();
  error = m_error;
  return ok;
}
bool SwitchDatabase::commitPending(std::string& error) {
  std::lock_guard<std::recursive_mutex> lock(m_ioMutex);
  error.clear();
  if (!m_pending || m_unsynced) return true; // Nothing durable yet, or mid-transaction.
  const bool ok = commitWithClosedWriters();
  error = m_error;
  return ok;
}
void SwitchDatabase::recordFileError(const char* operation, int code) {
  if (!operation) { m_error.clear(); return; }
  const std::string native = m_nativeError ? m_nativeError() : std::string();
  m_error = std::string(operation) + ": errno=" + std::to_string(code) +
      " (" + std::strerror(code) + ")";
  if (!native.empty()) m_error += "; " + native;
}
}
