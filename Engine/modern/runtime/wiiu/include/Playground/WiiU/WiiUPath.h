#ifndef PLAYGROUND_WIIU_PATH_H
#define PLAYGROUND_WIIU_PATH_H

#include <filesystem>
#include <cstdint>
#include <string>

namespace playground::wiiu_runtime {

// CafeOS has a much smaller practical path budget than the Android assets
// assume. Encode long components while leaving the logical engine path intact.
// The result is deterministic and idempotent so bootstrap and runtime lookups
// use exactly the same physical name.
std::filesystem::path
encodeStoragePath(const std::filesystem::path &logicalPath);

bool isRegularFile(const std::filesystem::path &path);
bool freeStorageBytes(const std::filesystem::path &path, std::uint64_t &bytes);

bool removeStorageTree(const std::filesystem::path &path, std::string &error);
bool clearStorageTree(const std::filesystem::path &path, std::string &error);

bool cleanupStaleAssetGenerations(const std::filesystem::path &cacheRoot,
                                  const std::filesystem::path &current,
                                  std::size_t &removed, std::string &error);

} // namespace playground::wiiu_runtime

#endif
