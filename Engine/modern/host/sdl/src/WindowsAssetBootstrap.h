#ifndef PLAYGROUND_WINDOWS_ASSET_BOOTSTRAP_H
#define PLAYGROUND_WINDOWS_ASSET_BOOTSTRAP_H

#if defined(_WIN32)

#include <cstdint>
#include <filesystem>
#include <string>

namespace playground::windows {

struct RuntimePaths {
  std::string installRoot;
  std::string externalRoot;
};

struct BootstrapOptions {
  std::filesystem::path bundleDirectory;
  std::filesystem::path dataRoot;
  bool showProgress{true};
  std::uint64_t abortProcessAfterBytes{};
};

bool getDefaultBootstrapOptions(BootstrapOptions &options, std::string &error);
bool prepareBundledAssets(const BootstrapOptions &options, RuntimePaths &paths,
                          std::string &error);
void attachParentConsole();
void showBootstrapError(const std::string &error);

} // namespace playground::windows

#endif

#endif
