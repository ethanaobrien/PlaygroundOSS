#ifndef PLAYGROUND_WIIU_STORAGE_H
#define PLAYGROUND_WIIU_STORAGE_H

#include <cstdint>
#include <filesystem>
#include <string>

namespace playground::wiiu_runtime {

struct StorageRoots {
  std::filesystem::path user;
  std::filesystem::path installContainer;
  std::filesystem::path content;
  std::filesystem::path state;
};

class WiiUStorage {
public:
  WiiUStorage() = default;
  ~WiiUStorage();

  WiiUStorage(const WiiUStorage &) = delete;
  WiiUStorage &operator=(const WiiUStorage &) = delete;

  bool mount(std::string &error);
  bool commit(std::string &error);
  void unmount();

  const StorageRoots &roots() const { return m_roots; }
  std::uint8_t accountSlot() const { return m_slot; }
  std::uint32_t persistentId() const { return m_persistentId; }

private:
  StorageRoots m_roots;
  std::uint8_t m_slot{};
  std::uint32_t m_persistentId{};
  bool m_actInitialized{};
  bool m_saveInitialized{};
};

bool hashFileSha256(const std::filesystem::path &path,
                    std::string &hexDigest, std::string &error);

} // namespace playground::wiiu_runtime

#endif
