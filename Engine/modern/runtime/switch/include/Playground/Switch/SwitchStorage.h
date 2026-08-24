#ifndef PLAYGROUND_SWITCH_STORAGE_H
#define PLAYGROUND_SWITCH_STORAGE_H

#include <filesystem>
#include <string>
#include <cstdint>

namespace playground::switch_runtime {

enum class StorageMode { InstalledTitle, DevelopmentNro };

struct StorageRoots {
  StorageMode mode{StorageMode::DevelopmentNro};
  std::filesystem::path state;
  std::filesystem::path cache;
  std::filesystem::path installContainer;
  std::filesystem::path content;
};

// Horizon requires an explicit commit for SaveData and CacheStorage.  Every
// writer (downloads, SQLite, preferences and lifecycle shutdown) uses this
// boundary so commits to the same mounted device cannot race.
bool commitMountedDevice(const char *mountName, std::string &error);
bool freeCacheStorageBytes(std::uint64_t &bytes, std::string &error);
std::string describeLastFileError();
std::string describeFileError(int errorNumber);

class SwitchStorage {
public:
  SwitchStorage();
  ~SwitchStorage();
  SwitchStorage(const SwitchStorage &) = delete;
  SwitchStorage &operator=(const SwitchStorage &) = delete;

  bool mountInstalledTitle(std::string &error);
  bool mountDevelopmentNro(const std::filesystem::path &explicitSdRoot,
                           std::string &error);
  bool commitState(std::string &error);
  bool commitCache(std::string &error);
  // CacheStorage contains only replaceable downloads and extracted bundled
  // assets. Recreate it when an incomplete bootstrap tree is structurally
  // undeletable; account SaveData is deliberately unaffected.
  bool resetCache(std::string &error);
  bool removeTree(const std::filesystem::path &path, std::string &error);
  bool removeCacheTree(const std::filesystem::path &path, std::string &error);
  // Commit both transactional mounts before releasing them. Unmount still
  // completes after a commit failure so service/account teardown is balanced;
  // callers receive the durability failure instead of silently discarding it.
  bool unmount(std::string *error = nullptr);
  const StorageRoots &roots() const { return m_roots; }
  bool mounted() const { return m_mounted; }

private:
  StorageRoots m_roots;
  bool m_mounted{};
  bool m_accountReady{};
  bool m_stateMounted{};
  bool m_cacheMounted{};
};

bool hashFileSha256(const std::filesystem::path &, std::string &digest,
                    std::string &error);

} // namespace playground::switch_runtime

#endif
