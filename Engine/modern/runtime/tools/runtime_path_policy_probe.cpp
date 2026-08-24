#include "Playground/Runtime/RuntimePlatform.h"
#include "Playground/Switch/SwitchPath.h"
#include "CKLBDatabase.h"
#include "CPFInterface.h"
#include "encryptFile.h"
#include "sqlite3.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

bool check(bool condition, const char *message) {
  if (!condition) {
    std::fprintf(stderr, "runtime-path-policy-probe: %s\n", message);
    std::fflush(nullptr);
    std::_Exit(1);
  }
  return condition;
}

std::string fullPath(playground::runtime::RuntimePlatform &platform,
                     const char *uri, bool &readOnly) {
  const char *value = platform.getFullPath(uri, &readOnly);
  std::string result = value ? value : "";
  delete[] value;
  return result;
}

std::string databasePath(playground::runtime::RuntimePlatform &platform,
                         const char *uri,
                         IPlatformRequest::DatabaseOpenMode mode,
                         bool &readOnly) {
  const char *value = platform.getDatabasePath(uri, mode, &readOnly);
  std::string result = value ? value : "";
  delete[] value;
  return result;
}

} // namespace

int main(int argc, char **argv) {
  if (argc != 2) {
    std::fprintf(stderr, "usage: %s scratch-root\n", argv[0]);
    return 64;
  }
  const std::filesystem::path scratch = argv[1];
  const std::filesystem::path install = scratch / "install";
  const std::filesystem::path content = scratch / "content";
  const std::filesystem::path state = scratch / "state";
  std::error_code error;
  std::filesystem::remove_all(scratch, error);
  std::filesystem::create_directories(install / "asset", error);
  std::filesystem::create_directories(content / "asset", error);
  std::filesystem::create_directories(state, error);
  if (!check(!error, "cannot create scratch roots"))
    return 1;

  int stateCommits = 0;
  int contentCommits = 0;
  int removals = 0;
  bool failNextContentCommit = false;
  playground::runtime::RuntimePlatform platform(
      install.string(), content.string(), state.string(), nullptr,
      [&] {
        ++stateCommits;
        return true;
      },
      [&] {
        ++contentCommits;
        if (failNextContentCommit) {
          failNextContentCommit = false;
          return false;
        }
        return true;
      }, {},
      [&](const std::filesystem::path &path, std::string &) {
        ++removals;
        std::error_code removeError;
        std::filesystem::remove_all(path, removeError);
        return !removeError;
      });

  bool readOnly = false;
  if (!check(fullPath(platform, "file://state/profile.db", readOnly) ==
                     (state / "profile.db").string() &&
                 !readOnly,
             "state URI did not select the private state root") ||
      !check(fullPath(platform, "file://external/profile.db", readOnly) ==
                     (content / "profile.db").string() &&
                 !readOnly,
             "ordinary external URI did not select the content root") ||
      !check(databasePath(platform, "file://external/profile.db",
                          IPlatformRequest::DatabaseOpenMode::ReadOnly,
                          readOnly) == (content / "profile.db").string() &&
                 !readOnly,
             "read-only external database did not select visible content") ||
      !check(databasePath(platform, "file://external/profile.db",
                          IPlatformRequest::DatabaseOpenMode::ReadWriteCreate,
                          readOnly) == (state / "profile.db").string() &&
                 !readOnly,
             "writable external database did not select account state") ||
      !check(databasePath(platform, "file://state/settings.db",
                          IPlatformRequest::DatabaseOpenMode::ReadWriteCreate,
                          readOnly) == (state / "settings.db").string() &&
                 !readOnly,
             "explicit state database did not retain durable state") ||
      !check(fullPath(platform, "file://external/assets/cache.bin", readOnly) ==
                     (content / "assets/cache.bin").string() &&
                 !readOnly,
             "external URI did not select the content root") ||
      !check(fullPath(platform, "file://install/start.lua", readOnly) ==
                     (install / "start.lua").string() &&
                 readOnly,
             "install URI did not select the read-only install root"))
    return 1;

  std::ofstream(content / "copy-up.db") << "seeded-content";
  const int stateBeforeCopyUp = stateCommits;
  if (!check(databasePath(platform, "file://external/copy-up.db",
                          IPlatformRequest::DatabaseOpenMode::ReadWrite,
                          readOnly) == (state / "copy-up.db").string(),
             "writable database did not resolve to account state") ||
      !check(std::filesystem::exists(state / "copy-up.db") &&
                 stateCommits == stateBeforeCopyUp + 1,
             "writable database did not durably copy up existing content"))
    return 1;
  std::ifstream copiedDatabase(state / "copy-up.db");
  std::string copiedContents;
  copiedDatabase >> copiedContents;
  if (!check(copiedContents == "seeded-content",
             "database copy-up changed the stored bytes"))
    return 1;

  // A failed archive commit must never leave a visible or durable partial
  // upper-layer database. The implementation publishes through a unique
  // same-directory temporary object, removes the completed name after the
  // injected failure, and commits that rollback once storage recovers.
  const std::filesystem::path failureState = scratch / "failure-state";
  std::filesystem::create_directories(failureState, error);
  std::ofstream(content / "commit-failure") << "complete-lower-object";
  int failureCommits = 0;
  bool failNextCommit = false;
  playground::runtime::RuntimePlatform failurePlatform(
      install.string(), content.string(), failureState.string(), nullptr,
      [&] {
        ++failureCommits;
        if (failNextCommit) {
          failNextCommit = false;
          return false;
        }
        return true;
      });
  failureCommits = 0;
  failNextCommit = true;
  const std::string failedCopyUp = databasePath(
      failurePlatform, "file://external/commit-failure",
      IPlatformRequest::DatabaseOpenMode::ReadWrite, readOnly);
  bool temporaryRemains = false;
  for (const auto &entry : std::filesystem::directory_iterator(failureState)) {
    if (entry.path().filename().string().rfind(".pg-copyup-", 0) == 0)
      temporaryRemains = true;
  }
  if (!check(failedCopyUp.empty() && failureCommits == 2 &&
                 !std::filesystem::exists(failureState / "commit-failure") &&
                 !temporaryRemains,
             "failed database publication left a visible or temporary copy"))
    return 1;

  // Account SaveData is the upper layer, while CacheStorage is shared by the
  // title. Two selected users must therefore receive independent copy-ups of
  // the same lower object without modifying that lower object or each other.
  const std::filesystem::path accountA = scratch / "account-a";
  const std::filesystem::path accountB = scratch / "account-b";
  std::filesystem::create_directories(accountA, error);
  std::filesystem::create_directories(accountB, error);
  std::ofstream(content / "account-proof") << "shared-seed";
  int accountACommits = 0;
  int accountBCommits = 0;
  playground::runtime::RuntimePlatform platformA(
      install.string(), content.string(), accountA.string(), nullptr,
      [&] { ++accountACommits; return true; });
  playground::runtime::RuntimePlatform platformB(
      install.string(), content.string(), accountB.string(), nullptr,
      [&] { ++accountBCommits; return true; });
  // Runtime construction may durably create the engine's account identity;
  // isolate the copy-up boundary measured below.
  accountACommits = 0;
  accountBCommits = 0;
  const std::string accountAPath = databasePath(
      platformA, "file://external/account-proof",
      IPlatformRequest::DatabaseOpenMode::ReadWrite, readOnly);
  const std::string accountBPath = databasePath(
      platformB, "file://external/account-proof",
      IPlatformRequest::DatabaseOpenMode::ReadWrite, readOnly);
  std::ofstream(accountAPath, std::ios::trunc) << "account-a-value";
  std::ifstream accountBValue(accountBPath);
  std::ifstream sharedValue(content / "account-proof");
  std::string accountBText;
  std::string sharedText;
  accountBValue >> accountBText;
  sharedValue >> sharedText;
  if (!check(accountAPath == (accountA / "account-proof").string() &&
                 accountBPath == (accountB / "account-proof").string() &&
                 accountACommits == 1 && accountBCommits == 1 &&
                 accountBText == "shared-seed" && sharedText == "shared-seed",
             "database copy-up was not isolated by selected account"))
    return 1;

  std::ofstream(state / "delete-shadow") << "state";
  std::ofstream(content / "delete-shadow") << "content";
  const int stateBeforeOverlayDelete = stateCommits;
  const int contentBeforeOverlayDelete = contentCommits;
  if (!check(platform.removeFileOrFolder(
                 "file://external/delete-shadow") &&
                 !std::filesystem::exists(state / "delete-shadow") &&
                 !std::filesystem::exists(content / "delete-shadow") &&
                 stateCommits == stateBeforeOverlayDelete + 1 &&
                 contentCommits == contentBeforeOverlayDelete + 1,
             "logical deletion exposed a stale lower-layer object"))
    return 1;

  std::ofstream(state / "profile.db") << "private";
  std::ofstream(content / "profile.db") << "replaceable";
  if (!check(databasePath(platform, "file://external/profile.db",
                          IPlatformRequest::DatabaseOpenMode::ReadOnly,
                          readOnly) == (state / "profile.db").string() &&
                 !readOnly,
             "read-only database did not observe the account-state overlay") ||
      !check(fullPath(platform, "file://external/profile.db", readOnly) ==
                     (state / "profile.db").string(),
             "ordinary reads did not observe the account-state overlay"))
    return 1;

  std::ofstream(state / "state-only") << "state";
  std::ofstream(content / "content-only") << "content";
  std::vector<std::string> names;
  if (!check(platform.listFiles("file://external/", names),
             "external overlay directory could not be enumerated") ||
      !check(std::find(names.begin(), names.end(), "state-only") != names.end() &&
                 std::find(names.begin(), names.end(), "content-only") != names.end(),
             "external directory enumeration did not merge state and content"))
    return 1;

  std::ofstream(state / "rename-source") << "state";
  if (!check(platform.irename("file://external/rename-source",
                              "file://external/rename-destination") == 0 &&
                 std::filesystem::exists(state / "rename-destination") &&
                 !std::filesystem::exists(content / "rename-destination"),
             "external rename did not preserve the account-state layer"))
    return 1;

  std::ofstream(content / "rename-copy-up") << "lower-source";
  (void)databasePath(platform, "file://external/rename-copy-up",
                     IPlatformRequest::DatabaseOpenMode::ReadWrite,
                     readOnly);
  std::ofstream(content / "rename-copy-destination") << "lower-destination";

  // Lower-layer cleanup must be committed before the authoritative upper
  // object moves. A failed CacheStorage transaction may leave its mounted
  // view dirty, but it must not also rename the selected account's source and
  // then report the whole operation as failed.
  const int stateBeforeFailedOverlayRename = stateCommits;
  failNextContentCommit = true;
  if (!check(platform.irename("file://external/rename-copy-up",
                              "file://external/rename-copy-destination") != 0 &&
                 std::filesystem::exists(state / "rename-copy-up") &&
                 !std::filesystem::exists(state /
                                          "rename-copy-destination") &&
                 stateCommits == stateBeforeFailedOverlayRename,
             "failed lower-layer cleanup moved authoritative account state"))
    return 1;
  std::ofstream(content / "rename-copy-up") << "lower-source";
  std::ofstream(content / "rename-copy-destination") << "lower-destination";
  if (!check(platform.irename("file://external/rename-copy-up",
                              "file://external/rename-copy-destination") == 0 &&
                 !std::filesystem::exists(state / "rename-copy-up") &&
                 !std::filesystem::exists(content / "rename-copy-up") &&
                 std::filesystem::exists(state / "rename-copy-destination") &&
                 !std::filesystem::exists(content / "rename-copy-destination"),
             "copy-up rename exposed a stale lower-layer object"))
    return 1;

  std::ofstream(install / "asset/value.texb") << "install";
  if (!check(fullPath(platform, "asset://asset/value.texb", readOnly) ==
                     (install / "asset/value.texb").string() &&
                 readOnly,
             "asset fallback did not select install content"))
    return 1;
  std::ofstream(content / "asset/value.texb") << "downloaded";
  if (!check(fullPath(platform, "asset://asset/value.texb", readOnly) ==
                     (content / "asset/value.texb").string() &&
                 !readOnly,
             "downloaded asset did not override install content"))
    return 1;

  if (!check(fullPath(platform, "asset://asset/not-present.db_", readOnly)
                         .empty(),
             "missing asset unexpectedly resolved to an install path"))
    return 1;
  if (!check(databasePath(
                 platform, "asset://asset/not-present.db_",
                 IPlatformRequest::DatabaseOpenMode::ReadOnly, readOnly)
                 .empty(),
             "missing named database became a temporary database"))
    return 1;

  const int stateBeforeDatabaseCommit = stateCommits;
  const int contentBeforeDatabaseCommit = contentCommits;
  if (!check(platform.commitDatabasePath((state / "profile.db").c_str()) &&
                 stateCommits == stateBeforeDatabaseCommit + 1 &&
                 contentCommits == contentBeforeDatabaseCommit,
             "database durability did not select the state filesystem") ||
      !check(platform.commitDatabasePath((content / "cache.db").c_str()) &&
                 stateCommits == stateBeforeDatabaseCommit + 1 &&
                 contentCommits == contentBeforeDatabaseCommit + 1,
             "database durability did not select the content filesystem") ||
      !check(platform.commitDatabasePath(
                 (install / "read-only.db").c_str()) &&
                 stateCommits == stateBeforeDatabaseCommit + 1 &&
                 contentCommits == contentBeforeDatabaseCommit + 1,
             "database durability committed an unowned filesystem"))
    return 1;

  for (const char *invalid :
       {"file://state/../content/leak", "file://external/a/../../leak",
        "file://install/C:/escape", "asset://a\\escape"}) {
    if (!check(fullPath(platform, invalid, readOnly).empty(),
               "unsafe URI was accepted"))
      return 1;
  }

  std::ofstream(state / "old") << "state";
  const int stateBeforeRename = stateCommits;
  const int contentBeforeRename = contentCommits;
  if (!check(platform.irename("file://state/old", "file://state/new") == 0 &&
                 stateCommits == stateBeforeRename + 1 &&
                 contentCommits == contentBeforeRename,
             "state publication did not commit SaveData exactly once"))
    return 1;
  std::filesystem::create_directories(content / "assets", error);
  std::ofstream(content / "assets/old") << "content";
  if (!check(platform.irename("file://external/assets/old",
                              "file://external/assets/new") == 0 &&
                 stateCommits == stateBeforeRename + 1 &&
                 contentCommits == contentBeforeRename + 1,
             "content publication did not commit CacheStorage exactly once"))
    return 1;
  if (!check(platform.irename("file://state/new",
                              "file://external/assets/cross") != 0,
             "cross-device state publication was accepted"))
    return 1;

  std::filesystem::create_directories(content / "assets/removable/child",
                                      error);
  std::ofstream(content / "assets/removable/child/value") << "content";
  const int contentBeforeRemove = contentCommits;
  const int stateBeforeRemove = stateCommits;
  const int removalsBeforeRemove = removals;
  if (!check(platform.removeFileOrFolder(
                 "file://external/assets/removable") &&
                 removals == removalsBeforeRemove + 1 &&
                 contentCommits == contentBeforeRemove + 1 &&
                 stateCommits == stateBeforeRemove &&
                 !std::filesystem::exists(content / "assets/removable"),
             "external removal did not clear and commit its single owner"))
    return 1;

  // Exercise the same logical/physical boundary used by installed Switch
  // builds. The engine sees and enumerates the original 70-byte name while
  // every native operation uses <=64-byte physical components. Rename and
  // removal must not leak the codec's continuation directories.
  {
    const std::filesystem::path encodedInstall = scratch / "encoded-install";
    const std::filesystem::path encodedContent = scratch / "encoded-content";
    const std::filesystem::path encodedState = scratch / "encoded-state";
    std::filesystem::create_directories(encodedInstall, error);
    std::filesystem::create_directories(encodedContent, error);
    std::filesystem::create_directories(encodedState, error);
    const auto mapper = playground::switch_runtime::encodeStoragePath;
    playground::runtime::RuntimePlatform encodedPlatform(
        encodedInstall.string(), encodedContent.string(),
        encodedState.string(), nullptr, [] { return true; },
        [] { return true; }, mapper,
        [](const std::filesystem::path &path, std::string &removeError) {
          std::error_code fsError;
          std::filesystem::remove_all(path, fsError);
          if (fsError)
            removeError = fsError.message();
          return !fsError;
        }, {}, playground::switch_runtime::listStorageDirectory);
    const std::string longName =
        "generic-" + std::string(57, 'x') + ".imag";
    if (!check(longName.size() == 70,
               "encoded-path fixture is not 70 bytes"))
      return 1;
    const std::filesystem::path logicalInstallAsset =
        encodedInstall / "asset" / longName;
    const std::filesystem::path physicalInstallAsset =
        mapper(logicalInstallAsset);
    std::filesystem::create_directories(physicalInstallAsset.parent_path(),
                                        error);
    std::ofstream(physicalInstallAsset) << "encoded-install";
    const std::string installUri = "asset://asset/" + longName;
    if (!check(fullPath(encodedPlatform, installUri.c_str(), readOnly) ==
                   physicalInstallAsset.string() && readOnly,
               "encoded install asset did not resolve from its logical URI"))
      return 1;
    const std::string sourceUri = "file://external/" + longName;
    const std::string source = fullPath(
        encodedPlatform, sourceUri.c_str(), readOnly);
    const std::filesystem::path sourcePath(source);
    std::filesystem::create_directories(sourcePath.parent_path(), error);
    std::ofstream(sourcePath) << "encoded-content";
    std::vector<std::string> encodedNames;
    if (!check(!error && encodedPlatform.listFiles("file://external/",
                                                   encodedNames) &&
                   std::find(encodedNames.begin(), encodedNames.end(),
                             longName) != encodedNames.end(),
               "encoded storage did not return its logical directory name"))
      return 1;
    const std::string renamedName =
        "renamed-" + std::string(57, 'y') + ".imag";
    const std::string renamedUri = "file://external/" + renamedName;
    if (!check(encodedPlatform.irename(sourceUri.c_str(),
                                       renamedUri.c_str()) == 0,
               "encoded storage rename failed") ||
        !check(encodedPlatform.removeFileOrFolder(renamedUri.c_str()),
               "encoded storage removal failed"))
      return 1;
    encodedNames.clear();
    if (!check(encodedPlatform.listFiles("file://external/", encodedNames) &&
                   std::find(encodedNames.begin(), encodedNames.end(),
                             longName) == encodedNames.end() &&
                   std::find(encodedNames.begin(), encodedNames.end(),
                             renamedName) == encodedNames.end(),
               "encoded storage leaked a removed logical name"))
      return 1;
  }

  CPFInterface::getInstance().setPlatformRequest(&platform);
  initNMAsset(0);
  if (!check(initializeEncryptedSQLiteVFS(),
             "encrypted SQLite VFS could not initialize"))
    return 1;
  const std::string encryptedUri = "file://external/encrypted-roundtrip";
  const std::string encryptedState = databasePath(
      platform, encryptedUri.c_str(),
      IPlatformRequest::DatabaseOpenMode::ReadWriteCreate, readOnly);
  sqlite3 *database = nullptr;
  char *sqliteError = nullptr;
  const int createOpen = sqlite3_open_v2(
      encryptedState.c_str(), &database,
      SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr);
  if (!check(createOpen == SQLITE_OK,
             database ? sqlite3_errmsg(database) :
                        "could not open encrypted account database"))
    return 1;
  const int createExec = sqlite3_exec(
      database,
      "PRAGMA journal_mode=OFF;"
      "CREATE TABLE proof(value INTEGER);"
      "INSERT INTO proof VALUES(73);",
      nullptr, nullptr, &sqliteError);
  if (!check(createExec == SQLITE_OK,
             sqliteError ? sqliteError : "could not write encrypted database") ||
      !check(sqlite3_close(database) == SQLITE_OK,
             "could not close encrypted account database"))
    return 1;
  sqlite3_free(sqliteError);

  const std::filesystem::path encryptedContent = content / "encrypted-roundtrip";
  std::filesystem::copy_file(encryptedState, encryptedContent,
                             std::filesystem::copy_options::overwrite_existing,
                             error);
  std::filesystem::remove(encryptedState, error);
  const std::string copiedState = databasePath(
      platform, encryptedUri.c_str(), IPlatformRequest::DatabaseOpenMode::ReadWrite,
      readOnly);
  int selectedValue = 0;
  const auto readValue = [](void *context, int columns, char **values,
                            char **) {
    if (columns == 1 && values && values[0])
      *static_cast<int *>(context) = std::atoi(values[0]);
    return 0;
  };
  database = nullptr;
  sqliteError = nullptr;
  if (!check(sqlite3_open_v2(copiedState.c_str(), &database,
                             SQLITE_OPEN_READWRITE, nullptr) == SQLITE_OK &&
                 sqlite3_exec(database, "SELECT value FROM proof;", readValue,
                              &selectedValue, &sqliteError) == SQLITE_OK &&
                 selectedValue == 73 && sqlite3_close(database) == SQLITE_OK,
             sqliteError ? sqliteError :
                           "encrypted database did not survive copy-up"))
    return 1;
  sqlite3_free(sqliteError);

  std::filesystem::remove_all(scratch, error);
  std::puts("runtime-path-policy-probe passed");
  std::fflush(nullptr);
  std::_Exit(0);
}
