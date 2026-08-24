#ifndef PLAYGROUND_SWITCH_PATH_H
#define PLAYGROUND_SWITCH_PATH_H

#include <filesystem>
#include <string>
#include <vector>

namespace playground::switch_runtime {

// Horizon SaveData and CacheStorage accept paths far longer than an individual
// 64-byte component. Preserve the logical application namespace by losslessly
// splitting only oversized (or reserved-prefix) components into a private
// sequence of <=64-byte physical components. This is a filesystem encoding,
// not an application-asset rename policy.
std::filesystem::path
encodeStoragePath(const std::filesystem::path &logicalPath);

// Enumerate one encoded physical directory and return its immediate logical
// children. Encoded continuation directories are implementation details and
// never leak to the engine.
bool listStorageDirectory(const std::filesystem::path &physicalDirectory,
                          std::vector<std::string> &logicalNames,
                          std::string &error);

} // namespace playground::switch_runtime

#endif
