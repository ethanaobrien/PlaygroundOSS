#ifndef PLAYGROUND_SWITCH_SQLITE_H
#define PLAYGROUND_SWITCH_SQLITE_H

#include <cstring>
#include <string>

namespace playground {
namespace switch_runtime {
inline const char *switchDurabilityVfsName() {
  return "playground-switch-save-commit";
}

inline bool isSwitchEncryptedAssetUri(const char *uri) {
  return uri &&
         (std::strncmp(uri, "asset://", 8) == 0 ||
          std::strncmp(uri, "file://asset/", 13) == 0 ||
          std::strncmp(uri, "file://install/", 15) == 0);
}

inline const char *selectSwitchDatabaseVfs(const char *logicalUri,
                                           const char *resolvedPath,
                                           bool readOnly) {
  const bool encryptedAsset =
      resolvedPath && isSwitchEncryptedAssetUri(logicalUri);
  return encryptedAsset || readOnly ? nullptr : switchDurabilityVfsName();
}

bool initializeSwitchSqliteDurability(std::string &error);
void shutdownSwitchSqliteDurability();
} // namespace switch_runtime
} // namespace playground

#endif
