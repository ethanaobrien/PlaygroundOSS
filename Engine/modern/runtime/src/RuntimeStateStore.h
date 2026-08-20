#ifndef PLAYGROUND_RUNTIME_RUNTIME_STATE_STORE_H
#define PLAYGROUND_RUNTIME_RUNTIME_STATE_STORE_H

#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>

namespace playground::runtime {

class RuntimeStateStore {
public:
  using Commit = std::function<bool()>;
  explicit RuntimeStateStore(std::filesystem::path path,
                             Commit commit = {});

  std::string get(const std::string &key) const;
  bool set(const std::string &key, const std::string &value);
  bool erase(const std::string &key);

private:
  bool load();
  bool saveLocked() const;

  std::filesystem::path m_path;
  Commit m_commit;
  mutable std::mutex m_mutex;
  std::unordered_map<std::string, std::string> m_values;
};

} // namespace playground::runtime

#endif
