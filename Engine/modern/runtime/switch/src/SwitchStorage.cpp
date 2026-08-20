#include "Playground/Switch/SwitchStorage.h"
#include "Playground/Switch/StorageProvision.h"

#include <switch.h>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <mutex>

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
constexpr u64 SaveDataAvailableSize = 0x4000;

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

Result createAccountSaveData(u64 applicationId, AccountUid user) {
  FsSaveDataAttribute attribute{};
  attribute.application_id = applicationId;
  attribute.uid = user;
  attribute.save_data_type = FsSaveDataType_Account;
  FsSaveDataCreationInfo creation{};
  creation.save_data_size = SaveDataSize;
  creation.journal_size = SaveDataJournalSize;
  creation.available_size = SaveDataAvailableSize;
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
  creation.available_size = SaveDataAvailableSize;
  creation.owner_id = applicationId;
  creation.save_data_space_id = FsSaveDataSpaceId_User;
  FsSaveDataMetaInfo metadata{};
  return fsCreateSaveDataFileSystem(&attribute, &creation, &metadata);
}

bool hasYuzu1734MisorderedCreateArtifact(u64 applicationId, s64 journalSize) {
  FsSaveDataInfoReader reader{};
  if (R_FAILED(fsOpenSaveDataInfoReader(&reader, FsSaveDataSpaceId_User)))
    return false;

  bool found = false;
  for (;;) {
    FsSaveDataInfo entries[16]{};
    s64 count = 0;
    const Result result =
        fsSaveDataInfoReaderRead(&reader, entries, 16, &count);
    if (R_FAILED(result) || count <= 0)
      break;
    for (s64 index = 0; index < count; ++index) {
      const FsSaveDataInfo &entry = entries[index];
      // yuzu 1734 decodes command 22 as CreationInfo, Attribute, u128,
      // whereas Horizon/libnx use Attribute, CreationInfo, MetaInfo. The
      // standard request therefore leaves this unique synthetic record:
      // System type in User space, owner as system-save ID, and the journal
      // and available sizes reinterpreted as the UID. Never select this path
      // based on a generic emulator probe or a failed mount alone.
      if (entry.save_data_space_id == FsSaveDataSpaceId_User &&
          entry.save_data_type == FsSaveDataType_System &&
          entry.system_save_data_id == applicationId &&
          entry.uid.uid[0] == static_cast<u64>(journalSize) &&
          entry.uid.uid[1] == SaveDataAvailableSize) {
        found = true;
        break;
      }
    }
    if (found || count < 16)
      break;
  }
  fsSaveDataInfoReaderClose(&reader);
  return found;
}

Result createWithYuzu1734FieldOrder(const FsSaveDataAttribute &attribute,
                                    const FsSaveDataCreationInfo &creation) {
  struct Yuzu1734CreateInput {
    FsSaveDataCreationInfo creation;
    FsSaveDataAttribute attribute;
    FsSaveDataMetaInfo metadata;
  } input{creation, attribute, {}};
  static_assert(sizeof(Yuzu1734CreateInput) == sizeof(FsSaveDataCreationInfo) +
                                                   sizeof(FsSaveDataAttribute) +
                                                   sizeof(FsSaveDataMetaInfo));
  return serviceDispatchIn(fsGetServiceSession(), 22, input);
}

Result createAccountSaveDataYuzu1734(u64 applicationId, AccountUid user) {
  FsSaveDataAttribute attribute{};
  attribute.application_id = applicationId;
  attribute.uid = user;
  attribute.save_data_type = FsSaveDataType_Account;
  FsSaveDataCreationInfo creation{};
  creation.save_data_size = SaveDataSize;
  creation.journal_size = SaveDataJournalSize;
  creation.available_size = SaveDataAvailableSize;
  creation.owner_id = applicationId;
  creation.save_data_space_id = FsSaveDataSpaceId_User;
  return createWithYuzu1734FieldOrder(attribute, creation);
}

Result createCacheStorageYuzu1734(u64 applicationId) {
  FsSaveDataAttribute attribute{};
  attribute.application_id = applicationId;
  attribute.save_data_type = FsSaveDataType_Cache;
  attribute.save_data_index = CacheIndex;
  FsSaveDataCreationInfo creation{};
  creation.save_data_size = CacheStorageSize;
  creation.journal_size = CacheStorageJournalSize;
  creation.available_size = SaveDataAvailableSize;
  creation.owner_id = applicationId;
  creation.save_data_space_id = FsSaveDataSpaceId_User;
  return createWithYuzu1734FieldOrder(attribute, creation);
}

template <typename Mount, typename Create, typename Yuzu1734Create>
bool mountOrProvision(const char *description, Mount mount, Create create,
                      Yuzu1734Create yuzu1734Create, u64 applicationId,
                      s64 journalSize, bool &usedYuzu1734Compatibility,
                      std::string &error) {
  const StorageProvisionOutcome outcome =
      provisionStorage(mount, create, [](std::uint32_t result) {
        return targetNotFound(result);
      });
  if (outcome.mounted) {
    // The malformed command-22 record survives after yuzu has created the
    // correctly ordered save.  Keep recognizing that exact fingerprint on
    // subsequent launches so old yuzu also receives the filesystem-operation
    // compatibility it needs (notably its missing RenameDirectory command).
    if (hasYuzu1734MisorderedCreateArtifact(applicationId, journalSize))
      usedYuzu1734Compatibility = true;
    return true;
  }
  if (outcome.creationAttempted && targetNotFound(outcome.retryResult) &&
      hasYuzu1734MisorderedCreateArtifact(applicationId, journalSize)) {
    const Result compatibilityResult = yuzu1734Create();
    const Result compatibilityRetry = mount();
    if (R_SUCCEEDED(compatibilityRetry)) {
      usedYuzu1734Compatibility = true;
      std::fprintf(stderr,
                   "%s: applied verified yuzu 1734 command-22 field-order "
                   "compatibility (create=0x%08x)\n",
                   description, compatibilityResult);
      return true;
    }
    char text[320];
    std::snprintf(text, sizeof(text),
                  "%s was absent; standard creation returned 0x%08x, its "
                  "retry returned 0x%08x, yuzu-1734 compatibility creation "
                  "returned 0x%08x, and its retry returned 0x%08x",
                  description, outcome.creationResult, outcome.retryResult,
                  compatibilityResult, compatibilityRetry);
    error = text;
    return false;
  }
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

SwitchStorage::SwitchStorage() = default;
SwitchStorage::~SwitchStorage() { unmount(); }

bool SwitchStorage::mountInstalledTitle(std::string &error) {
  unmount();
  if (!envIsNso()) {
    error = "Installed-title storage was requested from an NRO";
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
          [=] { return createAccountSaveDataYuzu1734(applicationId, user); },
          applicationId, SaveDataJournalSize, m_yuzu1734Compatibility, error)) {
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
          [=] { return createCacheStorageYuzu1734(applicationId); },
          applicationId, CacheStorageJournalSize, m_yuzu1734Compatibility,
          error)) {
    unmount();
    return false;
  }
  m_cacheMounted = true;
  m_roots.mode = StorageMode::InstalledTitle;
  m_roots.state = std::string(StateMount) + ":/user";
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

void SwitchStorage::unmount() {
  std::string ignored;
  if (m_stateMounted)
    commitState(ignored);
  if (m_cacheMounted)
    commitCache(ignored);
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
  m_yuzu1734Compatibility = false;
  m_roots = {};
}

} // namespace playground::switch_runtime
