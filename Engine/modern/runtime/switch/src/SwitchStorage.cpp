#include "Playground/Switch/SwitchStorage.h"
#include "Playground/Switch/StorageProvision.h"

#include <switch.h>

#include <array>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <utility>

namespace playground::switch_runtime {
namespace {

constexpr const char *StateMount = "pgstate";
constexpr const char *CacheMount = "pgcache";
constexpr u16 CacheIndex = 0;
constexpr u32 FsResultModule = 2;
constexpr s64 SaveDataSize = PLAYGROUND_SWITCH_SAVE_SIZE;
constexpr s64 SaveDataJournalSize = PLAYGROUND_SWITCH_SAVE_JOURNAL_SIZE;
constexpr s64 CacheStorageSize = PLAYGROUND_SWITCH_CACHE_SIZE;
constexpr s64 CacheStorageJournalSize = PLAYGROUND_SWITCH_CACHE_JOURNAL_SIZE;
// libnx retains the older `available_size` field name at this IPC offset.
// Horizon's SaveDataCreationInfo names the same field `block_size`; the
// platform default is 16 KiB. It is filesystem geometry, not free capacity.
constexpr u64 SaveDataBlockSize = 0x4000;

static_assert(SaveDataSize > 0 && SaveDataJournalSize > 0);
static_assert(CacheStorageSize > 0 && CacheStorageJournalSize > 0);

std::mutex g_stateCommitMutex;
std::mutex g_cacheCommitMutex;
std::mutex g_otherCommitMutex;

std::string resultError(const char *operation, Result result) {
  char text[160];
  std::snprintf(text, sizeof(text), "%s failed with result 0x%08x", operation,
                result);
  return text;
}

bool programId(u64 &value) {
  return R_SUCCEEDED(
      svcGetInfo(&value, InfoType_ProgramId, CUR_PROCESS_HANDLE, 0));
}

bool targetNotFound(Result result) {
  // FS result 1002 is returned when OpenSaveDataFileSystem cannot find the
  // requested account save or CacheStorage.  Do not provision on any other
  // error: permissions, corruption, media failure and mount conflicts must
  // remain visible rather than being mistaken for a first launch.
  return R_MODULE(result) == FsResultModule && R_DESCRIPTION(result) == 1002;
}

bool pathNotFound(Result result) {
  // FS 2-1 is PathNotFound. Recursive cleanup is intentionally idempotent.
  return R_MODULE(result) == FsResultModule && R_DESCRIPTION(result) == 1;
}

bool pathIsBelowRoot(const std::string &path, const std::string &root) {
  if (root.empty() || path.size() <= root.size() ||
      path.compare(0, root.size(), root) != 0)
    return false;
  return root.back() == '/' || path[root.size()] == '/';
}

Result createAccountSaveData(u64 applicationId, AccountUid user) {
  FsSaveDataAttribute attribute{};
  attribute.application_id = applicationId;
  attribute.uid = user;
  attribute.save_data_type = FsSaveDataType_Account;
  FsSaveDataCreationInfo creation{};
  creation.save_data_size = SaveDataSize;
  creation.journal_size = SaveDataJournalSize;
  creation.available_size = SaveDataBlockSize;
  creation.owner_id = applicationId;
  creation.save_data_space_id = FsSaveDataSpaceId_User;
  FsSaveDataMetaInfo metadata{};
  return fsCreateSaveDataFileSystem(&attribute, &creation, &metadata);
}

Result createCacheStorage(u64 applicationId) {
  FsSaveDataAttribute attribute{};
  attribute.application_id = applicationId;
  attribute.save_data_type = FsSaveDataType_Cache;
  attribute.save_data_index = CacheIndex;
  FsSaveDataCreationInfo creation{};
  creation.save_data_size = CacheStorageSize;
  creation.journal_size = CacheStorageJournalSize;
  creation.available_size = SaveDataBlockSize;
  creation.owner_id = applicationId;
  creation.save_data_space_id = FsSaveDataSpaceId_User;
  FsSaveDataMetaInfo metadata{};
  return fsCreateSaveDataFileSystem(&attribute, &creation, &metadata);
}

Result deleteCacheStorage(u16 index) {
  // IFileSystemProxy::DeleteCacheStorage (command 33, Horizon 5.0.0+) is the
  // title-scoped operation for disposable CacheStorage. libnx exposes the
  // adjacent create/open calls but not this wrapper.
  if (hosversionBefore(5, 0, 0))
    return MAKERESULT(Module_Libnx, LibnxError_IncompatSysVer);
  return serviceDispatchIn(fsGetServiceSession(), 33, index);
}

template <typename Mount, typename Create>
bool mountOrProvision(const char *description, Mount mount, Create create,
                      std::string &error) {
  const StorageProvisionOutcome outcome =
      provisionStorage(mount, create, [](std::uint32_t result) {
        return targetNotFound(result);
      });
  if (outcome.mounted)
    return true;
  if (!outcome.creationAttempted) {
    error = resultError(description, outcome.initialResult);
    return false;
  }

  char text[256];
  std::snprintf(text, sizeof(text),
                "%s was absent; creation returned 0x%08x and the retry "
                "returned 0x%08x",
                description, outcome.creationResult, outcome.retryResult);
  error = text;
  return false;
}

bool createRoots(StorageRoots &roots, std::string &error) {
  std::error_code fsError;
  std::filesystem::create_directories(roots.state, fsError);
  if (!fsError)
    std::filesystem::create_directories(roots.installContainer, fsError);
  if (!fsError)
    std::filesystem::create_directories(roots.content, fsError);
  if (fsError) {
    error = "Could not create application storage roots: " + fsError.message();
    return false;
  }
  return true;
}

} // namespace

bool commitMountedDevice(const char *mountName, std::string &error) {
  if (!mountName || !mountName[0]) {
    error = "Storage commit requires a mounted device name";
    return false;
  }
  std::mutex *mutex = &g_otherCommitMutex;
  if (std::strcmp(mountName, StateMount) == 0)
    mutex = &g_stateCommitMutex;
  else if (std::strcmp(mountName, CacheMount) == 0)
    mutex = &g_cacheCommitMutex;
  std::lock_guard<std::mutex> lock(*mutex);
  const Result result = fsdevCommitDevice(mountName);
  if (R_FAILED(result)) {
    error = resultError("Storage commit", result);
    return false;
  }
  return true;
}

bool freeCacheStorageBytes(std::uint64_t &bytes, std::string &error) {
  FsFileSystem *filesystem = fsdevGetDeviceFileSystem(CacheMount);
  if (!filesystem) {
    error = "CacheStorage free-space query requires a mounted cache";
    return false;
  }
  s64 available = 0;
  const Result result = fsFsGetFreeSpace(filesystem, "/", &available);
  if (R_FAILED(result)) {
    error = resultError("CacheStorage free-space query", result);
    return false;
  }
  if (available < 0) {
    error = "CacheStorage returned a negative free-space value";
    return false;
  }
  bytes = static_cast<std::uint64_t>(available);
  return true;
}

std::string describeFileError(int errorNumber) {
  const Result result = fsdevGetLastResult();
  char text[160];
  std::snprintf(text, sizeof(text),
                "errno=%d (%s), Horizon result 0x%08x (module=%u, "
                "description=%u)",
                errorNumber, std::strerror(errorNumber), result,
                R_MODULE(result), R_DESCRIPTION(result));
  return text;
}

std::string describeLastFileError() { return describeFileError(errno); }

SwitchStorage::SwitchStorage() = default;
SwitchStorage::~SwitchStorage() {
  std::string error;
  if (!unmount(&error) && !error.empty())
    std::fprintf(stderr, "Switch storage destructor: %s\n", error.c_str());
}

bool SwitchStorage::mountInstalledTitle(std::string &error) {
  unmount();
  if (!envIsNso()) {
    error = "Installed-title storage was requested from an NRO";
    return false;
  }
  // libnx learns the Horizon version from set:sys during process startup.
  // Its CacheStorage wrappers deliberately return IncompatSysVer without an
  // FS request when that lookup was denied and the version remains 0.0.0.
  if (hosversionGet() == 0) {
    error = "Horizon version discovery failed; verify set:sys access in the "
            "installed NPDM";
    return false;
  }
  u64 applicationId = 0;
  if (!programId(applicationId)) {
    error = "Could not determine the installed application ID";
    return false;
  }
  Result result = accountInitialize(AccountServiceType_Application);
  if (R_FAILED(result)) {
    error = resultError("accountInitialize", result);
    return false;
  }
  m_accountReady = true;
  AccountUid user{};
  result = accountGetPreselectedUser(&user);
  if (R_FAILED(result) || !accountUidIsValid(&user)) {
    error = R_FAILED(result) ? resultError("accountGetPreselectedUser", result)
                             : "The title was launched without a selected user";
    unmount();
    return false;
  }
  if (!mountOrProvision(
          "fsdevMountSaveData",
          [=] { return fsdevMountSaveData(StateMount, applicationId, user); },
          [=] { return createAccountSaveData(applicationId, user); },
          error)) {
    unmount();
    return false;
  }
  m_stateMounted = true;
  if (!mountOrProvision(
          "fsdevMountCacheStorage",
          [=] {
            return fsdevMountCacheStorage(CacheMount, applicationId,
                                          CacheIndex);
          },
          [=] { return createCacheStorage(applicationId); },
          error)) {
    unmount();
    return false;
  }
  m_cacheMounted = true;
  m_roots.mode = StorageMode::InstalledTitle;
  // Durable engine state lives at the account SaveData root. Replaceable
  // content and installed-asset generations use CacheStorage below.
  m_roots.state = std::string(StateMount) + ":/";
  m_roots.cache = std::string(CacheMount) + ":/";
  m_roots.installContainer = m_roots.cache / "application";
  m_roots.content = m_roots.cache / "content";
  if (!createRoots(m_roots, error)) {
    unmount();
    return false;
  }
  m_mounted = true;
  if (!commitState(error) || !commitCache(error)) {
    unmount();
    return false;
  }
  return true;
}

bool SwitchStorage::mountDevelopmentNro(
    const std::filesystem::path &explicitSdRoot, std::string &error) {
  unmount();
  if (envIsNso()) {
    error = "SD development storage is forbidden for an installed title";
    return false;
  }
  if (explicitSdRoot.empty() ||
      explicitSdRoot.string().rfind("sdmc:/", 0) != 0) {
    error = "NRO development storage requires an explicit sdmc:/ path";
    return false;
  }
  m_roots.mode = StorageMode::DevelopmentNro;
  m_roots.state = explicitSdRoot / "state";
  m_roots.cache = explicitSdRoot / "cache";
  m_roots.installContainer = m_roots.cache / "application";
  m_roots.content = m_roots.cache / "content";
  if (!createRoots(m_roots, error))
    return false;
  m_mounted = true;
  return true;
}

bool SwitchStorage::commitState(std::string &error) {
  if (!m_stateMounted)
    return true;
  return commitMountedDevice(StateMount, error);
}

bool SwitchStorage::commitCache(std::string &error) {
  if (!m_cacheMounted)
    return true;
  return commitMountedDevice(CacheMount, error);
}

bool SwitchStorage::resetCache(std::string &error) {
  if (!m_cacheMounted || m_roots.mode != StorageMode::InstalledTitle) {
    error = "CacheStorage reset requires a mounted installed-title cache";
    return false;
  }
  u64 applicationId = 0;
  if (!programId(applicationId)) {
    error = "Could not determine the installed application ID for cache "
            "recovery";
    return false;
  }
  if (fsdevUnmountDevice(CacheMount) < 0) {
    error = "Could not unmount damaged CacheStorage";
    return false;
  }
  m_cacheMounted = false;
  Result result = deleteCacheStorage(CacheIndex);
  if (R_FAILED(result)) {
    error = resultError("DeleteCacheStorage", result);
    return false;
  }
  result = createCacheStorage(applicationId);
  if (R_FAILED(result)) {
    error = resultError("CacheStorage recreation", result);
    return false;
  }
  result = fsdevMountCacheStorage(CacheMount, applicationId, CacheIndex);
  if (R_FAILED(result)) {
    error = resultError("CacheStorage remount", result);
    return false;
  }
  m_cacheMounted = true;
  if (!createRoots(m_roots, error) || !commitCache(error))
    return false;
  return true;
}

bool SwitchStorage::removeCacheTree(const std::filesystem::path &path,
                                    std::string &error) {
  const std::string full = path.lexically_normal().generic_string();
  const std::string cacheRoot = m_roots.cache.generic_string();
  if (!pathIsBelowRoot(full, cacheRoot)) {
    error = "CacheStorage removal rejected a path outside the cache root";
    return false;
  }
  return removeTree(path, error);
}

bool SwitchStorage::removeTree(const std::filesystem::path &path,
                               std::string &error) {
  const std::string full = path.lexically_normal().generic_string();
  const std::string cacheRoot = m_roots.cache.generic_string();
  const std::string stateRoot = m_roots.state.generic_string();
  if (m_mounted && m_roots.mode == StorageMode::DevelopmentNro) {
    const bool insideCache = pathIsBelowRoot(full, cacheRoot);
    const bool insideState = pathIsBelowRoot(full, stateRoot);
    if (!insideCache && !insideState) {
      error = "Development storage removal rejected a root or outside path";
      return false;
    }
    std::error_code filesystemError;
    std::filesystem::remove_all(path, filesystemError);
    if (filesystemError) {
      error = "Development storage removal failed: " +
              filesystemError.message();
      return false;
    }
    return true;
  }
  const bool cache = m_cacheMounted && pathIsBelowRoot(full, cacheRoot);
  const bool state = m_stateMounted && pathIsBelowRoot(full, stateRoot);
  if (!cache && !state) {
    error = "Native storage removal rejected a mount root or outside path";
    return false;
  }
  const std::string &root = cache ? cacheRoot : stateRoot;
  std::string nativePath = full.substr(root.size());
  if (nativePath.empty() || nativePath.front() != '/')
    nativePath.insert(nativePath.begin(), '/');
  if (nativePath.size() >= FS_MAX_PATH) {
    error = "CacheStorage removal path exceeds Horizon's path limit";
    return false;
  }
  // libnx advertises FS_MAX_PATH bytes in the IPC pointer buffer regardless
  // of the C string's logical length. Give Horizon a fully backed and
  // zero-padded buffer rather than exposing bytes beyond std::string storage.
  std::array<char, FS_MAX_PATH> nativeBuffer{};
  std::memcpy(nativeBuffer.data(), nativePath.data(), nativePath.size());
  const char *mount = cache ? CacheMount : StateMount;
  FsFileSystem *filesystem = fsdevGetDeviceFileSystem(mount);
  if (!filesystem) {
    error = "Native storage removal could not resolve its mounted filesystem";
    return false;
  }
  // Do not query entry type first. Physical hardware has accepted deletion
  // while rejecting the redundant lookup. DeleteFile handles ordinary files;
  // recursive deletion handles directories. A missing path is idempotent.
  const Result fileResult = fsFsDeleteFile(filesystem, nativeBuffer.data());
  if (R_SUCCEEDED(fileResult))
    return true;
  const Result directoryResult =
      fsFsDeleteDirectoryRecursively(filesystem, nativeBuffer.data());
  if (R_SUCCEEDED(directoryResult))
    return true;
  if (pathNotFound(fileResult) && pathNotFound(directoryResult))
    return true;
  {
    char text[384];
    std::snprintf(text, sizeof(text),
                  "Native storage removal failed (file=0x%08x, "
                  "directory=0x%08x) for path '%s' (length=%zu)",
                  R_VALUE(fileResult), R_VALUE(directoryResult),
                  nativePath.c_str(), nativePath.size());
    error = text;
    return false;
  }
}

bool SwitchStorage::unmount(std::string *error) {
  bool committed = true;
  std::string failure;
  auto recordFailure = [&](const char *storage, const std::string &detail) {
    committed = false;
    if (!failure.empty())
      failure += "; ";
    failure += storage;
    failure += ": ";
    failure += detail;
  };
  std::string commitError;
  if (m_stateMounted && !commitState(commitError))
    recordFailure("SaveData commit", commitError);
  commitError.clear();
  if (m_cacheMounted && !commitCache(commitError))
    recordFailure("CacheStorage commit", commitError);
  if (m_cacheMounted) {
    fsdevUnmountDevice(CacheMount);
    m_cacheMounted = false;
  }
  if (m_stateMounted) {
    fsdevUnmountDevice(StateMount);
    m_stateMounted = false;
  }
  if (m_accountReady) {
    accountExit();
    m_accountReady = false;
  }
  m_mounted = false;
  m_roots = {};
  if (error)
    *error = std::move(failure);
  return committed;
}

} // namespace playground::switch_runtime
