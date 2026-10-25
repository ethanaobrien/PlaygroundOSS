#pragma once
#include <cstdio>
#include <filesystem>
#include <string>
#include <unistd.h>

namespace playground::switch_runtime {
// Only used by the diagnostic sampler. Two fixed filenames, bounded disk use;
// no logging from game/audio/IO callbacks and no unbounded in-memory queue.
class BoundedDiagnosticLog {
public:
  explicit BoundedDiagnosticLog(std::filesystem::path directory,
                               std::size_t limit = 512 * 1024)
      : current(directory / "runtime-current.log"),
        previous(directory / "runtime-previous.log"), limit(limit) {}
  bool begin() {
    std::error_code ec;
    std::filesystem::create_directories(current.parent_path(), ec);
    return !ec && rotate();
  }
  bool append(const std::string& text) {
    if (text.size() > limit) return false;
    if (bytes + text.size() > limit && !rotate()) return false;
    FILE* file = std::fopen(current.c_str(), "ab");
    if (!file) return false;
    bool ok = std::fwrite(text.data(), 1, text.size(), file) == text.size();
    if (std::fflush(file) != 0 || fsync(fileno(file)) != 0) ok = false;
    if (std::fclose(file) != 0) ok = false;
    if (ok) bytes += text.size();
    return ok;
  }
private:
  bool rotate() {
    std::error_code ec;
    const bool exists = std::filesystem::exists(current, ec);
    if (ec) return false;
    if (exists) {
      std::filesystem::remove(previous, ec);
      if (ec) return false;
      std::filesystem::rename(current, previous, ec);
      if (ec) return false;
    }
    bytes = 0;
    return true;
  }
  std::filesystem::path current, previous;
  std::size_t limit, bytes{};
};
}
