#include "Playground/Switch/SwitchSqlite.h"
#include "Playground/Switch/SwitchStorage.h"

#include "sqlite3.h"

#include <cstddef>
#include <cstdio>
#include <cstring>

namespace playground::switch_runtime {
namespace {

constexpr std::size_t Alignment = alignof(std::max_align_t);

struct WrappedFile {
  sqlite3_file base{};
  sqlite3_file *real{};
  const sqlite3_io_methods *methods{};
  sqlite3_io_methods wrapper{};
  bool saveData{};
  bool dirty{};
};

sqlite3_vfs g_vfs{};
sqlite3_vfs *g_realVfs{};
bool g_registered{};

WrappedFile *self(sqlite3_file *file) {
  return reinterpret_cast<WrappedFile *>(file);
}

bool isSaveDataPath(const char *path) {
  return path && (std::strncmp(path, "pgstate:", 8) == 0 ||
                  std::strstr(path, "/pgstate:/") != nullptr);
}

int commitSaveData() {
  std::string error;
  if (commitMountedDevice("pgstate", error))
    return SQLITE_OK;
  std::fprintf(stderr, "SQLite SaveData commit failed: %s\n", error.c_str());
  return SQLITE_IOERR_FSYNC;
}

int closeFile(sqlite3_file *file) {
  auto *f = self(file);
  const bool commit = f->saveData && f->dirty;
  const int result = f->methods->xClose(f->real);
  f->base.pMethods = nullptr;
  return result == SQLITE_OK && commit ? commitSaveData() : result;
}
int readFile(sqlite3_file *f, void *p, int n, sqlite3_int64 o) {
  auto *w = self(f);
  const int result = w->methods->xRead(w->real, p, n, o);
  if (result != SQLITE_OK && result != SQLITE_IOERR_SHORT_READ)
    std::fprintf(stderr, "SQLite VFS read failed: result=%d bytes=%d offset=%lld\n",
                 result, n, static_cast<long long>(o));
  return result;
}
int writeFile(sqlite3_file *f, const void *p, int n, sqlite3_int64 o) {
  auto *w = self(f);
  int result = w->methods->xWrite(w->real, p, n, o);
  if (result == SQLITE_OK)
    w->dirty = true;
  else
    std::fprintf(stderr,
                 "SQLite VFS write failed: result=%d bytes=%d offset=%lld\n",
                 result, n, static_cast<long long>(o));
  return result;
}
int truncateFile(sqlite3_file *f, sqlite3_int64 n) {
  auto *w = self(f);
  int result = w->methods->xTruncate(w->real, n);
  if (result == SQLITE_OK)
    w->dirty = true;
  return result;
}
int syncFile(sqlite3_file *f, int flags) {
  auto *w = self(f);
  int result = w->methods->xSync(w->real, flags);
  if (result != SQLITE_OK || !w->saveData || !w->dirty)
    return result;
  result = commitSaveData();
  if (result == SQLITE_OK)
    w->dirty = false;
  return result;
}
int fileSize(sqlite3_file *f, sqlite3_int64 *n) {
  auto *w = self(f);
  return w->methods->xFileSize(w->real, n);
}
int lockFile(sqlite3_file *f, int n) {
  auto *w = self(f);
  const int result = w->methods->xLock(w->real, n);
  if (result != SQLITE_OK)
    std::fprintf(stderr, "SQLite VFS lock failed: result=%d level=%d\n",
                 result, n);
  return result;
}
int unlockFile(sqlite3_file *f, int n) {
  auto *w = self(f);
  const int result = w->methods->xUnlock(w->real, n);
  if (result != SQLITE_OK)
    std::fprintf(stderr, "SQLite VFS unlock failed: result=%d level=%d\n",
                 result, n);
  return result;
}
int reserved(sqlite3_file *f, int *n) {
  auto *w = self(f);
  const int result = w->methods->xCheckReservedLock(w->real, n);
  if (result != SQLITE_OK)
    std::fprintf(stderr, "SQLite VFS reserved-lock check failed: result=%d\n",
                 result);
  return result;
}
int control(sqlite3_file *f, int op, void *p) {
  auto *w = self(f);
  return w->methods->xFileControl(w->real, op, p);
}
int sector(sqlite3_file *f) {
  auto *w = self(f);
  return w->methods->xSectorSize(w->real);
}
int characteristics(sqlite3_file *f) {
  auto *w = self(f);
  return w->methods->xDeviceCharacteristics(w->real);
}
int shmMap(sqlite3_file *f, int page, int size, int extend,
           void volatile **output) {
  auto *w = self(f);
  return w->methods->xShmMap
             ? w->methods->xShmMap(w->real, page, size, extend, output)
             : SQLITE_IOERR;
}
int shmLock(sqlite3_file *f, int offset, int count, int flags) {
  auto *w = self(f);
  return w->methods->xShmLock
             ? w->methods->xShmLock(w->real, offset, count, flags)
             : SQLITE_IOERR;
}
void shmBarrier(sqlite3_file *f) {
  auto *w = self(f);
  if (w->methods->xShmBarrier)
    w->methods->xShmBarrier(w->real);
}
int shmUnmap(sqlite3_file *f, int deleteFlag) {
  auto *w = self(f);
  return w->methods->xShmUnmap ? w->methods->xShmUnmap(w->real, deleteFlag)
                               : SQLITE_OK;
}

int openFile(sqlite3_vfs *, const char *name, sqlite3_file *file, int flags,
             int *outFlags) {
  auto *w = self(file);
  std::memset(w, 0, sizeof(*w));
  const std::size_t offset =
      (sizeof(WrappedFile) + Alignment - 1) & ~(Alignment - 1);
  w->real = reinterpret_cast<sqlite3_file *>(
      reinterpret_cast<unsigned char *>(file) + offset);
  int result = g_realVfs->xOpen(g_realVfs, name, w->real, flags, outFlags);
  if (result != SQLITE_OK) {
    std::fprintf(stderr,
                 "SQLite VFS open failed: result=%d flags=0x%x path=%s\n",
                 result, flags, name ? name : "(temporary)");
    return result;
  }
  w->methods = w->real->pMethods;
  w->saveData = isSaveDataPath(name) &&
                (flags & (SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE));
  w->wrapper = *w->methods;
  w->wrapper.xClose = closeFile;
  w->wrapper.xRead = readFile;
  w->wrapper.xWrite = writeFile;
  w->wrapper.xTruncate = truncateFile;
  w->wrapper.xSync = syncFile;
  w->wrapper.xFileSize = fileSize;
  w->wrapper.xLock = lockFile;
  w->wrapper.xUnlock = unlockFile;
  w->wrapper.xCheckReservedLock = reserved;
  w->wrapper.xFileControl = control;
  w->wrapper.xSectorSize = sector;
  w->wrapper.xDeviceCharacteristics = characteristics;
  if (w->wrapper.iVersion >= 2) {
    w->wrapper.xShmMap = shmMap;
    w->wrapper.xShmLock = shmLock;
    w->wrapper.xShmBarrier = shmBarrier;
    w->wrapper.xShmUnmap = shmUnmap;
  }
  w->base.pMethods = &w->wrapper;
  return SQLITE_OK;
}
int deleteFile(sqlite3_vfs *, const char *name, int syncDirectory) {
  int result = g_realVfs->xDelete(g_realVfs, name, syncDirectory);
  return result == SQLITE_OK && isSaveDataPath(name) ? commitSaveData()
                                                     : result;
}
int accessFile(sqlite3_vfs *, const char *name, int flags, int *result) {
  return g_realVfs->xAccess(g_realVfs, name, flags, result);
}
int fullPath(sqlite3_vfs *, const char *name, int size, char *output) {
  // newlib devoptab paths are already absolute in the form "device:/path".
  // SQLite's generic Unix VFS treats them as relative and prefixes '/', which
  // makes the mounted device invisible to the engine's encrypted VFS wrapper.
  if (name && (std::strncmp(name, "pgstate:/", 9) == 0 ||
               std::strncmp(name, "pgcache:/", 9) == 0)) {
    const std::size_t length = std::strlen(name);
    if (length + 1 > static_cast<std::size_t>(size))
      return SQLITE_CANTOPEN;
    std::memcpy(output, name, length + 1);
    return SQLITE_OK;
  }
  return g_realVfs->xFullPathname(g_realVfs, name, size, output);
}
void *dlOpen(sqlite3_vfs *, const char *name) {
  return g_realVfs->xDlOpen(g_realVfs, name);
}
void dlError(sqlite3_vfs *, int size, char *error) {
  g_realVfs->xDlError(g_realVfs, size, error);
}
void (*dlSym(sqlite3_vfs *, void *handle, const char *name))(void) {
  return g_realVfs->xDlSym(g_realVfs, handle, name);
}
void dlClose(sqlite3_vfs *, void *handle) {
  g_realVfs->xDlClose(g_realVfs, handle);
}
int randomness(sqlite3_vfs *, int size, char *output) {
  return g_realVfs->xRandomness(g_realVfs, size, output);
}
int sleepVfs(sqlite3_vfs *, int microseconds) {
  return g_realVfs->xSleep(g_realVfs, microseconds);
}
int currentTime(sqlite3_vfs *, double *time) {
  return g_realVfs->xCurrentTime(g_realVfs, time);
}
int lastError(sqlite3_vfs *, int size, char *error) {
  return g_realVfs->xGetLastError
             ? g_realVfs->xGetLastError(g_realVfs, size, error)
             : 0;
}
int currentTime64(sqlite3_vfs *, sqlite3_int64 *time) {
  return g_realVfs->xCurrentTimeInt64(g_realVfs, time);
}
int setSystemCall(sqlite3_vfs *, const char *name,
                  sqlite3_syscall_ptr function) {
  return g_realVfs->xSetSystemCall
             ? g_realVfs->xSetSystemCall(g_realVfs, name, function)
             : SQLITE_NOTFOUND;
}
sqlite3_syscall_ptr getSystemCall(sqlite3_vfs *, const char *name) {
  return g_realVfs->xGetSystemCall ? g_realVfs->xGetSystemCall(g_realVfs, name)
                                   : nullptr;
}
const char *nextSystemCall(sqlite3_vfs *, const char *name) {
  return g_realVfs->xNextSystemCall
             ? g_realVfs->xNextSystemCall(g_realVfs, name)
             : nullptr;
}

} // namespace

bool initializeSwitchSqliteDurability(std::string &error) {
  if (g_registered)
    return true;
  // Horizon's devoptab does not implement POSIX advisory file locks (fcntl
  // returns ENOSYS), while SQLite's default "unix" VFS treats that as
  // SQLITE_IOERR_LOCK.  The engine is the sole process which can access its
  // mounted SaveData and CacheStorage, so SQLite's provided unix-none VFS is
  // the correct base. It preserves all I/O and durability operations while
  // omitting only cross-process locks that Horizon cannot provide here.
  g_realVfs = sqlite3_vfs_find("unix-none");
  if (!g_realVfs) {
    error = "SQLite has no unix-none VFS for mounted Switch storage";
    return false;
  }
  g_vfs = *g_realVfs;
  g_vfs.pNext = nullptr;
  g_vfs.zName = switchDurabilityVfsName();
  const std::size_t offset =
      (sizeof(WrappedFile) + Alignment - 1) & ~(Alignment - 1);
  g_vfs.szOsFile = static_cast<int>(offset + g_realVfs->szOsFile);
  g_vfs.pAppData = g_realVfs;
  g_vfs.xOpen = openFile;
  g_vfs.xDelete = deleteFile;
  g_vfs.xAccess = accessFile;
  g_vfs.xFullPathname = fullPath;
  g_vfs.xDlOpen = dlOpen;
  g_vfs.xDlError = dlError;
  g_vfs.xDlSym = dlSym;
  g_vfs.xDlClose = dlClose;
  g_vfs.xRandomness = randomness;
  g_vfs.xSleep = sleepVfs;
  g_vfs.xCurrentTime = currentTime;
  g_vfs.xGetLastError = lastError;
  if (g_vfs.iVersion >= 2)
    g_vfs.xCurrentTimeInt64 = currentTime64;
  if (g_vfs.iVersion >= 3) {
    g_vfs.xSetSystemCall = setSystemCall;
    g_vfs.xGetSystemCall = getSystemCall;
    g_vfs.xNextSystemCall = nextSystemCall;
  }
  const int result = sqlite3_vfs_register(&g_vfs, 1);
  if (result != SQLITE_OK) {
    error = "SQLite VFS registration failed: " + std::to_string(result);
    g_realVfs = nullptr;
    return false;
  }
  g_registered = true;
  return true;
}
void shutdownSwitchSqliteDurability() {
  if (!g_registered)
    return;
  sqlite3_vfs_unregister(&g_vfs);
  g_registered = false;
  g_realVfs = nullptr;
}

} // namespace playground::switch_runtime
