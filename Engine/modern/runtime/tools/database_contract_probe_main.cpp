#include "Playground/Switch/SwitchDatabase.h"
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
void databaseProbeCheckpoint(const char* text) {
  std::printf("checkpoint: %s\n", text);
  std::fflush(stdout);
}
std::string databaseProbeNativeError() { return {}; }
bool databaseProbeSingleNativeHandle(const char* native) {
  unsigned count = 0;
  for (const auto& entry : std::filesystem::directory_iterator("/proc/self/fd")) {
    std::error_code error;
    const auto target = std::filesystem::read_symlink(entry.path(), error);
    if (!error && target == native) ++count;
  }
  if (count != 1) std::fprintf(stderr, "Expected one native handle, found %u: %s\n", count, native);
  return count == 1;
}

// Model Horizon's archive-wide rule using actual process descriptors, not the
// adapter's bookkeeping. A flush does not make a writable handle acceptable.
static bool commitWithoutWriters(const std::filesystem::path& state, std::string& error) {
  const std::string prefix = state.string() + "/";
  for (const auto& entry : std::filesystem::directory_iterator("/proc/self/fd")) {
    std::error_code ec;
    const auto path = std::filesystem::read_symlink(entry.path(), ec).string();
    if (ec || path.compare(0, prefix.size(), prefix)) continue;
    const int flags = fcntl(std::stoi(entry.path().filename().string()), F_GETFL);
    if (flags >= 0 && (flags & O_ACCMODE) != O_RDONLY) {
      error = "WriteModeFileNotClosed: " + path;
      std::fprintf(stderr, "%s\n", error.c_str());
      return false;
    }
  }
  return true;
}

int runDatabaseContractProbe(const std::filesystem::path&, const std::filesystem::path&,
    const std::filesystem::path&, const playground::switch_runtime::SwitchDatabase::Commit&, bool);

int main(int argc, char** argv) {
  const bool verify = argc == 3 && std::string(argv[1]) == "--verify";
  if (argc != 2 && !verify) { std::fprintf(stderr, "usage: %s [--verify] scratch-directory\n", argv[0]); std::_Exit(64); }
  const auto root = std::filesystem::absolute(argv[verify ? 2 : 1]);
  std::error_code error;
  if (!verify && !std::filesystem::create_directory(root, error)) {
    std::fprintf(stderr, "Scratch directory must be new: %s\n", root.string().c_str());
    std::_Exit(1);
  }
  const int result = runDatabaseContractProbe(root / "install", root / "content",
      root / "state", [&](std::string& failure) {
        return commitWithoutWriters(root / "state", failure);
      }, verify);
  std::fflush(nullptr);
  // This isolated probe does not start the legacy game's process singletons.
  std::_Exit(result);
}
