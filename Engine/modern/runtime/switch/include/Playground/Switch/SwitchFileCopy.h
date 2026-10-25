#ifndef PLAYGROUND_SWITCH_FILE_COPY_H
#define PLAYGROUND_SWITCH_FILE_COPY_H

#include <array>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <string>
#include <unistd.h>

namespace playground::switch_runtime {

// Copy bytes, not POSIX ownership/permissions. Horizon device files do not
// implement the metadata operations used by std::filesystem::copy_file.
// Destination must be new; an existing object is never truncated or removed.
inline bool copyFileBytes(const std::string& source, const std::string& target,
                          std::string& error) {
  auto fail = [&](const char* operation, int code) {
    error = std::string(operation) + ": " + std::strerror(code);
    return false;
  };
  FILE* input = std::fopen(source.c_str(), "rb");
  if (!input) return fail("Copy source open", errno);
  const int fd = ::open(target.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0666);
  if (fd < 0) {
    const int code = errno;
    std::fclose(input);
    return fail("Copy destination open", code);
  }
  FILE* output = ::fdopen(fd, "wb");
  if (!output) {
    const int code = errno;
    ::close(fd);
    std::fclose(input);
    std::remove(target.c_str());
    return fail("Copy destination stream", code);
  }
  bool ok = true;
  std::array<unsigned char, 16384> buffer{};
  for (;;) {
    const auto count = std::fread(buffer.data(), 1, buffer.size(), input);
    if (std::ferror(input)) { ok = fail("Copy read", errno ? errno : EIO); break; }
    if (std::fwrite(buffer.data(), 1, count, output) != count) {
      ok = fail("Copy write", errno ? errno : EIO); break;
    }
    if (count < buffer.size()) break;
  }
  if (std::fclose(input) != 0 && ok) ok = fail("Copy source close", errno);
  if (std::fclose(output) != 0 && ok) ok = fail("Copy destination close", errno);
  if (!ok) std::remove(target.c_str());
  return ok;
}
}
#endif
