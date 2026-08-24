#ifndef PLAYGROUND_WIIU_SQLITE_H
#define PLAYGROUND_WIIU_SQLITE_H

#include <cstring>

namespace playground::wiiu_runtime {

inline const char *wiiuDurabilityVfsName() {
  return "playground-wiiu-durable";
}

inline bool isWiiUEncryptedAssetUri(const char *uri) {
  return uri &&
         (std::strncmp(uri, "asset://", 8) == 0 ||
          std::strncmp(uri, "file://asset/", 13) == 0 ||
          std::strncmp(uri, "file://install/", 15) == 0);
}

inline const char *selectWiiUDatabaseVfs(const char *logicalUri,
                                         const char *resolvedPath,
                                         bool readOnly) {
  // Downloaded master databases live under writable external storage but are
  // still addressed through asset:// and retain the engine's encrypted page
  // format. Keep those on the encrypted default VFS. Only genuine user/cache
  // databases use the durable unencrypted VFS.
  const bool encryptedAsset =
      resolvedPath && isWiiUEncryptedAssetUri(logicalUri);
  return encryptedAsset || readOnly ? nullptr : wiiuDurabilityVfsName();
}

} // namespace playground::wiiu_runtime

#endif
