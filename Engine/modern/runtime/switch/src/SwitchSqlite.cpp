#include "Playground/Switch/SwitchSqlite.h"
#include "Playground/Switch/SwitchStorage.h"

#include "sqlite3.h"

#include <cstring>
#include <cerrno>
#include <sys/types.h>

namespace playground::switch_runtime {
namespace {

sqlite3_vfs g_switchVfs{};
bool g_registered{};

// libnx/newlib device paths (for example pgstate:/settings.db) are
// already absolute. SQLite's Unix VFS otherwise treats the colon form as a
// relative path and prefixes the process working directory.
int passthroughFullPathname(sqlite3_vfs *, const char *path, int outputSize,
                            char *output) {
  if (!path || outputSize <= 0)
    return SQLITE_CANTOPEN;
  const std::size_t length = std::strlen(path);
  if (length + 1 > static_cast<std::size_t>(outputSize))
    return SQLITE_CANTOPEN;
  std::memcpy(output, path, length + 1);
  return SQLITE_OK;
}

} // namespace

bool initializeSwitchSqlitePaths(std::string &error) {
  if (g_registered)
    return true;

  // This is a path-canonicalization base, not the database I/O layer. The
  // engine subsequently makes its existing encrypted wrapper the default VFS,
  // exactly as it does on Android and as the first hardware-working Switch
  // draft did. unix-none contributes the remaining path/time/random helpers
  // without requiring advisory POSIX locks that Horizon does not expose.
  sqlite3_vfs *base = sqlite3_vfs_find("unix-none");
  if (!base)
    base = sqlite3_vfs_find(nullptr);
  if (!base) {
    error = "SQLite has no Unix VFS for mounted Switch storage";
    return false;
  }

  g_switchVfs = *base;
  g_switchVfs.pNext = nullptr;
  g_switchVfs.zName = switchPathVfsName();
  g_switchVfs.xFullPathname = passthroughFullPathname;
  const int result = sqlite3_vfs_register(&g_switchVfs, 1);
  if (result != SQLITE_OK) {
    error = "SQLite VFS registration failed: " + std::to_string(result);
    return false;
  }
  g_registered = true;
  return true;
}

void shutdownSwitchSqlitePaths() {
  if (!g_registered)
    return;
  sqlite3_vfs_unregister(&g_switchVfs);
  g_registered = false;
}

} // namespace playground::switch_runtime

// SQLite's Unix backend references these optional POSIX calls even though the
// engine's encrypted VFS replaces its xOpen path and selects its established
// journal policy. libnx has no uid/gid permission model and intentionally
// does not provide either symbol.
// Keep the link shims beside the only component which requires them.
extern "C" int fchown(int, uid_t, gid_t) {
  errno = ENOSYS;
  return -1;
}

extern "C" mode_t umask(mode_t) { return 0; }
