#ifndef PLAYGROUND_SWITCH_STORAGE_H
#define PLAYGROUND_SWITCH_STORAGE_H

#include <filesystem>
#include <string>

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
  void unmount();
  const StorageRoots &roots() const { return m_roots; }
  bool mounted() const { return m_mounted; }
  bool yuzu1734CompatibilityMode() const { return m_yuzu1734Compatibility; }

private:
  StorageRoots m_roots;
  bool m_mounted{};
  bool m_accountReady{};
  bool m_stateMounted{};
  bool m_cacheMounted{};
  bool m_yuzu1734Compatibility{};
};

bool hashFileSha256(const std::filesystem::path &, std::string &digest,
                    std::string &error);

} // namespace playground::switch_runtime

#endif
