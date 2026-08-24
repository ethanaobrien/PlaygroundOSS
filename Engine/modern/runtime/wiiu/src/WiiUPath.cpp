#include "Playground/WiiU/WiiUPath.h"

#include <coreinit/filesystem.h>
#include <mbedtls/sha256.h>

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cstring>
#include <string>

namespace playground::wiiu_runtime {
namespace {

constexpr std::size_t MaximumUnencodedComponent = 32;
constexpr char EncodingMarker[] = "~pg~";

FSClient *fileSystemClient() {
  alignas(64) static FSClient client;
  static bool initialized = false;
  static bool available = false;
  if (!initialized) {
    FSInit();
    available = FSAddClient(&client, FS_ERROR_FLAG_ALL) == FS_STATUS_OK;
    initialized = true;
  }
  return available ? &client : nullptr;
}

std::string nativeFileSystemPath(const std::filesystem::path &path) {
  std::string native = path.string();
  // "fs:" selects newlib's devoptab and is not part of a Cafe FS path.
  if (native.rfind("fs:", 0) == 0)
    native.erase(0, 3);
  return native;
}

bool isEncoded(const std::string &component) {
  const std::size_t marker = component.rfind(EncodingMarker);
  if (marker == std::string::npos ||
      component.size() - marker - (sizeof(EncodingMarker) - 1) != 32)
    return false;
  return std::all_of(component.begin() + marker + sizeof(EncodingMarker) - 1,
                     component.end(), [](unsigned char value) {
                       return std::isxdigit(value) != 0;
                     });
}

std::string encodeComponent(const std::string &component) {
  if (component.size() <= MaximumUnencodedComponent || isEncoded(component))
    return component;

  unsigned char digest[32]{};
  mbedtls_sha256_ret(
      reinterpret_cast<const unsigned char *>(component.data()),
      component.size(), digest, 0);
  static constexpr char Hex[] = "0123456789abcdef";
  const std::size_t retained =
      component.rfind(".staging-", 0) == 0 ? 9 : 8;
  std::string result = component.substr(0, retained);
  result += EncodingMarker;
  for (std::size_t index = 0; index < 16; ++index) {
    result.push_back(Hex[digest[index] >> 4]);
    result.push_back(Hex[digest[index] & 0xf]);
  }
  return result;
}

} // namespace

namespace {
bool removeStorageTreeImpl(const std::filesystem::path &path, bool removeRoot,
                           std::string &error) {
  struct stat status {};
  const std::string native = path.string();
  if (lstat(native.c_str(), &status) != 0) {
    if (errno == ENOENT)
      return true;
    error = "Could not inspect Wii U asset path: " +
            std::string(std::strerror(errno));
    return false;
  }
  if (!S_ISDIR(status.st_mode)) {
    if (unlink(native.c_str()) == 0 || errno == ENOENT)
      return true;
    error = "Could not remove Wii U asset file: " +
            std::string(std::strerror(errno));
    return false;
  }
  DIR *directory = opendir(native.c_str());
  if (!directory) {
    error = "Could not open Wii U asset directory: " +
            std::string(std::strerror(errno));
    return false;
  }
  bool valid = true;
  while (valid) {
    errno = 0;
    dirent *entry = readdir(directory);
    if (!entry) {
      if (errno) {
        error = "Could not enumerate Wii U asset directory: " +
                std::string(std::strerror(errno));
        valid = false;
      }
      break;
    }
    if (std::strcmp(entry->d_name, ".") == 0 ||
        std::strcmp(entry->d_name, "..") == 0)
      continue;
    valid = removeStorageTreeImpl(path / entry->d_name, true, error);
  }
  closedir(directory);
  if (!valid || !removeRoot)
    return valid;
  if (rmdir(native.c_str()) == 0 || errno == ENOENT)
    return true;
  error = "Could not remove Wii U asset directory: " +
          std::string(std::strerror(errno));
  return false;
}
} // namespace

bool removeStorageTree(const std::filesystem::path &path, std::string &error) {
  return removeStorageTreeImpl(path, true, error);
}

bool clearStorageTree(const std::filesystem::path &path, std::string &error) {
  struct stat status {};
  if (lstat(path.string().c_str(), &status) != 0 && errno == ENOENT)
    return true;
  if (!S_ISDIR(status.st_mode))
    return removeStorageTreeImpl(path, true, error);
  if (!removeStorageTreeImpl(path, false, error))
    return false;
  return true;
}

std::filesystem::path
encodeStoragePath(const std::filesystem::path &logicalPath) {
  const std::string input = logicalPath.string();
  std::string output;
  output.reserve(input.size());
  std::size_t start = 0;
  while (start <= input.size()) {
    const std::size_t separator = input.find('/', start);
    const std::size_t end =
        separator == std::string::npos ? input.size() : separator;
    output += encodeComponent(input.substr(start, end - start));
    if (separator == std::string::npos)
      break;
    output.push_back('/');
    start = separator + 1;
  }
  return output;
}

bool isRegularFile(const std::filesystem::path &path) {
  // Do not use newlib stat/fopen/open here. Cemu 2.6's newlib/devoptab bridge
  // corrupts PPC state while closing status probes made against mounted Wii U
  // volumes. Query the native FS service directly, as hardware applications
  // do, and keep one startup client for bootstrap validation.
  FSClient *client = fileSystemClient();
  if (!client)
    return false;
  alignas(64) FSCmdBlock command;
  FSInitCmdBlock(&command);
  const std::string native = nativeFileSystemPath(path);
  FSStat status{};
  return FSGetStat(client, &command, native.c_str(), &status,
                   FS_ERROR_FLAG_ALL) == FS_STATUS_OK &&
         (status.flags & FS_STAT_FILE) != 0;
}

bool freeStorageBytes(const std::filesystem::path &path, std::uint64_t &bytes) {
  bytes = 0;
  FSClient *client = fileSystemClient();
  if (!client)
    return false;
  alignas(64) FSCmdBlock command;
  FSInitCmdBlock(&command);
  const std::string native = nativeFileSystemPath(path);
  return FSGetFreeSpaceSize(client, &command, native.c_str(), &bytes,
                            FS_ERROR_FLAG_ALL) == FS_STATUS_OK;
}

bool cleanupStaleAssetGenerations(const std::filesystem::path &cacheRoot,
                                  const std::filesystem::path &current,
                                  std::size_t &removed, std::string &error) {
  removed = 0;
  DIR *directory = opendir(cacheRoot.string().c_str());
  if (!directory) {
    error = "Could not enumerate Wii U asset cache: " +
            std::string(std::strerror(errno));
    return false;
  }
  const std::string currentName = current.filename().string();
  errno = 0;
  while (dirent *entry = readdir(directory)) {
    const std::string name = entry->d_name;
    if (name == currentName ||
        (name.rfind("install-", 0) != 0 &&
         name.rfind(".staging-", 0) != 0))
      continue;
    std::string removeError;
    if (!removeStorageTree(cacheRoot / name, removeError)) {
      closedir(directory);
      error = "Could not remove stale Wii U asset generation: " + removeError;
      return false;
    }
    ++removed;
  }
  const int readError = errno;
  closedir(directory);
  if (readError) {
    error = "Could not finish enumerating Wii U asset cache: " +
            std::string(std::strerror(readError));
    return false;
  }
  return true;
}

} // namespace playground::wiiu_runtime
