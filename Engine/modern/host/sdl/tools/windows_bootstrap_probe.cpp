#include "WindowsAssetBootstrap.h"

#include <cstdio>
#include <cstdlib>

int main(int argc, char **argv) {
  if (argc != 3 && argc != 5) {
    std::fprintf(stderr,
                 "usage: %s bundle-directory data-root [--abort-after bytes]\n",
                 argv[0]);
    return 64;
  }
  playground::windows::BootstrapOptions options;
  options.bundleDirectory = std::filesystem::u8path(argv[1]);
  options.dataRoot = std::filesystem::u8path(argv[2]);
  options.showProgress = false;
  if (argc == 5) {
    if (std::string(argv[3]) != "--abort-after")
      return 64;
    options.abortProcessAfterBytes = std::strtoull(argv[4], nullptr, 10);
  }
  playground::windows::RuntimePaths paths;
  std::string error;
  if (!playground::windows::prepareBundledAssets(options, paths, error)) {
    std::fprintf(stderr, "bootstrap-probe: %s\n", error.c_str());
    return 1;
  }
  std::printf("install=%s\nexternal=%s\n", paths.installRoot.c_str(),
              paths.externalRoot.c_str());
  return 0;
}
